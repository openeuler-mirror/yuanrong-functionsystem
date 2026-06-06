//! Tenant resource quota — port of the C++ `function_master` `QuotaManagerActor`
//! / `QuotaConfig` (spec: `docs/superpowers/specs/2026-03-12-quota-manager-design.md`).
//!
//! This module provides the **config** and the **pure enforcement decision** (usage
//! tracking + over-quota detection + LIFO eviction selection + cooldown). The actual
//! eviction RPC and the cooldown delivery to the proxy are wired by the caller
//! (see `docs/analysis/170-quota-subsystem-parity-matrix.md`, step 3).

use std::collections::{BTreeMap, HashMap};

use serde::Deserialize;

/// C++ built-in defaults (`TenantQuota{32000, 65536, 10000}`).
pub const DEFAULT_CPU_MILLICORES: i64 = 32_000;
pub const DEFAULT_MEM_MB: i64 = 65_536;
pub const DEFAULT_COOLDOWN_MS: i64 = 10_000;

/// Per-tenant quota. Mirrors C++ `TenantQuota`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TenantQuota {
    pub cpu_millicores: i64,
    pub mem_limit_mb: i64,
    pub cooldown_ms: i64,
}

impl Default for TenantQuota {
    fn default() -> Self {
        Self {
            cpu_millicores: DEFAULT_CPU_MILLICORES,
            mem_limit_mb: DEFAULT_MEM_MB,
            cooldown_ms: DEFAULT_COOLDOWN_MS,
        }
    }
}

/// JSON shape (mirrors the C++ config example):
/// `{ "default": {"cpuMillicores":32000,"memMb":65536,"cooldownMs":10000},
///    "tenants": { "<id>": {"cpuMillicores":...,"memMb":...,"cooldownMs":...} } }`
#[derive(Debug, Clone, Deserialize)]
struct TenantQuotaJson {
    #[serde(rename = "cpuMillicores", alias = "cpu_millicores")]
    cpu_millicores: Option<i64>,
    #[serde(rename = "memMb", alias = "mem_mb", alias = "memLimitMb")]
    mem_mb: Option<i64>,
    #[serde(rename = "cooldownMs", alias = "cooldown_ms")]
    cooldown_ms: Option<i64>,
}

impl TenantQuotaJson {
    fn resolve(&self, fallback: TenantQuota) -> TenantQuota {
        TenantQuota {
            cpu_millicores: self.cpu_millicores.unwrap_or(fallback.cpu_millicores),
            mem_limit_mb: self.mem_mb.unwrap_or(fallback.mem_limit_mb),
            cooldown_ms: self.cooldown_ms.unwrap_or(fallback.cooldown_ms),
        }
    }
}

#[derive(Debug, Clone, Deserialize, Default)]
struct QuotaConfigJson {
    default: Option<TenantQuotaJson>,
    #[serde(default)]
    tenants: HashMap<String, TenantQuotaJson>,
}

/// Loaded quota configuration. Lookup order: per-tenant → default (mirrors C++ `GetQuota`).
#[derive(Debug, Clone)]
pub struct QuotaConfig {
    enabled: bool,
    default_quota: TenantQuota,
    per_tenant: HashMap<String, TenantQuota>,
}

impl Default for QuotaConfig {
    fn default() -> Self {
        Self::disabled()
    }
}

impl QuotaConfig {
    /// Enforcement disabled (C++: empty `--quota_config_file` path).
    pub fn disabled() -> Self {
        Self {
            enabled: false,
            default_quota: TenantQuota::default(),
            per_tenant: HashMap::new(),
        }
    }

    /// Load from JSON file. C++ parity: an empty path disables enforcement (warn);
    /// a configured-but-unreadable/invalid file is a hard error (caller should treat
    /// it as fatal, matching the C++ FATAL-on-bad-file behavior).
    pub fn load_from_file(path: &str) -> anyhow::Result<Self> {
        if path.trim().is_empty() {
            return Ok(Self::disabled());
        }
        let raw = std::fs::read_to_string(path)
            .map_err(|e| anyhow::anyhow!("read quota_config_file {path}: {e}"))?;
        let parsed: QuotaConfigJson = serde_json::from_str(&raw)
            .map_err(|e| anyhow::anyhow!("parse quota_config_file {path}: {e}"))?;
        let default_quota = parsed
            .default
            .map(|d| d.resolve(TenantQuota::default()))
            .unwrap_or_default();
        let per_tenant = parsed
            .tenants
            .into_iter()
            .map(|(id, q)| (id, q.resolve(default_quota)))
            .collect();
        Ok(Self {
            enabled: true,
            default_quota,
            per_tenant,
        })
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    /// Per-tenant quota with fallback to default (C++ `GetQuota`).
    pub fn quota_for_tenant(&self, tenant_id: &str) -> TenantQuota {
        self.per_tenant
            .get(tenant_id)
            .copied()
            .unwrap_or(self.default_quota)
    }
}

/// Outcome of an over-quota check: instances to evict (LIFO, newest first) and the
/// cooldown to apply to the tenant if any eviction was required.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct EnforceOutcome {
    pub evict: Vec<String>,
    pub cooldown_ms: Option<i64>,
}

#[derive(Default)]
struct TenantUsage {
    cpu_millicores: i64,
    mem_mb: i64,
    /// (arrival_ms, instance_id) ordered so the newest instance is `last` (LIFO eviction).
    sorted: BTreeMap<(i64, String), ()>,
    per_instance: HashMap<String, (i64, i64)>,
}

/// Pure usage tracker + enforcement decision. Mirrors C++ `QuotaManagerActor`
/// (`tenantUsage_`, `CheckAndEnforce`) without the actor/RPC machinery.
pub struct QuotaEnforcer {
    config: QuotaConfig,
    usage: HashMap<String, TenantUsage>,
}

impl QuotaEnforcer {
    pub fn new(config: QuotaConfig) -> Self {
        Self {
            config,
            usage: HashMap::new(),
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.config.is_enabled()
    }

    /// Account a newly-RUNNING instance and return the enforcement outcome.
    pub fn on_instance_running(
        &mut self,
        tenant_id: &str,
        instance_id: &str,
        cpu_millicores: i64,
        mem_mb: i64,
        arrival_ms: i64,
    ) -> EnforceOutcome {
        if !self.config.is_enabled() {
            return EnforceOutcome::default();
        }
        let u = self.usage.entry(tenant_id.to_string()).or_default();
        // Idempotent on duplicate RUNNING for the same instance.
        if u.per_instance.contains_key(instance_id) {
            return EnforceOutcome::default();
        }
        u.cpu_millicores += cpu_millicores;
        u.mem_mb += mem_mb;
        u.sorted.insert((arrival_ms, instance_id.to_string()), ());
        u.per_instance
            .insert(instance_id.to_string(), (cpu_millicores, mem_mb));
        self.check_and_enforce(tenant_id)
    }

    /// Account an EXITED/evicted instance (reduces usage if still tracked).
    pub fn on_instance_exited(&mut self, tenant_id: &str, instance_id: &str) {
        let Some(u) = self.usage.get_mut(tenant_id) else {
            return;
        };
        if let Some((cpu, mem)) = u.per_instance.remove(instance_id) {
            u.cpu_millicores = (u.cpu_millicores - cpu).max(0);
            u.mem_mb = (u.mem_mb - mem).max(0);
            u.sorted.retain(|(_, id), _| id != instance_id);
        }
    }

    fn check_and_enforce(&mut self, tenant_id: &str) -> EnforceOutcome {
        let quota = self.config.quota_for_tenant(tenant_id);
        let Some(u) = self.usage.get_mut(tenant_id) else {
            return EnforceOutcome::default();
        };
        let mut out = EnforceOutcome::default();
        // LIFO: evict the newest instances until usage is within quota.
        while (u.cpu_millicores > quota.cpu_millicores || u.mem_mb > quota.mem_limit_mb)
            && !u.sorted.is_empty()
        {
            let Some((key, ())) = u.sorted.pop_last() else {
                break;
            };
            let instance_id = key.1;
            if let Some((cpu, mem)) = u.per_instance.remove(&instance_id) {
                u.cpu_millicores = (u.cpu_millicores - cpu).max(0);
                u.mem_mb = (u.mem_mb - mem).max(0);
            }
            out.evict.push(instance_id);
        }
        if !out.evict.is_empty() {
            out.cooldown_ms = Some(quota.cooldown_ms);
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_quota_matches_cpp_builtin() {
        let q = TenantQuota::default();
        assert_eq!(q.cpu_millicores, 32_000);
        assert_eq!(q.mem_limit_mb, 65_536);
        assert_eq!(q.cooldown_ms, 10_000);
    }

    #[test]
    fn empty_path_disables_enforcement() {
        let c = QuotaConfig::load_from_file("").expect("empty ok");
        assert!(!c.is_enabled());
        // Disabled enforcer never evicts.
        let mut e = QuotaEnforcer::new(c);
        let out = e.on_instance_running("t", "i1", 999_999, 999_999, 1);
        assert_eq!(out, EnforceOutcome::default());
    }

    #[test]
    fn bad_path_is_error() {
        assert!(QuotaConfig::load_from_file("/nonexistent/quota.json").is_err());
    }

    #[test]
    fn json_load_default_and_per_tenant() {
        let dir = std::env::temp_dir();
        let path = dir.join(format!("quota_test_{}.json", std::process::id()));
        std::fs::write(
            &path,
            r#"{"default":{"cpuMillicores":4000,"memMb":8192,"cooldownMs":5000},
                "tenants":{"vip":{"cpuMillicores":16000}}}"#,
        )
        .unwrap();
        let c = QuotaConfig::load_from_file(path.to_str().unwrap()).expect("load");
        assert!(c.is_enabled());
        let d = c.quota_for_tenant("anyone");
        assert_eq!(d.cpu_millicores, 4000);
        assert_eq!(d.mem_limit_mb, 8192);
        assert_eq!(d.cooldown_ms, 5000);
        // per-tenant overrides cpu, inherits mem/cooldown from default.
        let v = c.quota_for_tenant("vip");
        assert_eq!(v.cpu_millicores, 16000);
        assert_eq!(v.mem_limit_mb, 8192);
        assert_eq!(v.cooldown_ms, 5000);
        let _ = std::fs::remove_file(&path);
    }

    fn cfg(cpu: i64, mem: i64) -> QuotaConfig {
        QuotaConfig {
            enabled: true,
            default_quota: TenantQuota {
                cpu_millicores: cpu,
                mem_limit_mb: mem,
                cooldown_ms: 7000,
            },
            per_tenant: HashMap::new(),
        }
    }

    #[test]
    fn under_quota_no_eviction() {
        let mut e = QuotaEnforcer::new(cfg(10_000, 10_000));
        let out = e.on_instance_running("t", "i1", 4000, 4000, 1);
        assert!(out.evict.is_empty());
        assert_eq!(out.cooldown_ms, None);
    }

    #[test]
    fn over_quota_evicts_newest_first_and_sets_cooldown() {
        let mut e = QuotaEnforcer::new(cfg(10_000, 1_000_000));
        assert!(e.on_instance_running("t", "old", 4000, 1, 1).evict.is_empty());
        assert!(e.on_instance_running("t", "mid", 4000, 1, 2).evict.is_empty());
        // third pushes cpu to 12000 > 10000 → evict the newest ("new") until under quota.
        let out = e.on_instance_running("t", "new", 4000, 1, 3);
        assert_eq!(out.evict, vec!["new".to_string()]);
        assert_eq!(out.cooldown_ms, Some(7000));
    }

    #[test]
    fn each_admission_evicts_at_most_the_newest() {
        // Incremental admission can only ever push usage over by the just-added instance,
        // so LIFO evicts exactly that newest instance and usage returns to the prior
        // (already-compliant) level. Mirrors C++ CheckAndEnforce running per RUNNING event.
        let mut e = QuotaEnforcer::new(cfg(5_000, 1_000_000));
        assert!(e.on_instance_running("t", "a", 3000, 1, 1).evict.is_empty());
        // b pushes to 6000 > 5000 → evict newest (b); usage back to 3000.
        let out_b = e.on_instance_running("t", "b", 3000, 1, 2);
        assert_eq!(out_b.evict, vec!["b".to_string()]);
        // big (9000) pushes 3000+9000=12000 > 5000 → evict only big; "a" survives.
        let out_big = e.on_instance_running("t", "big", 9000, 1, 3);
        assert_eq!(out_big.evict, vec!["big".to_string()]);
        assert_eq!(out_big.cooldown_ms, Some(7000));
        // "a" is still tracked: exiting it then re-admitting a fitting instance is clean.
        e.on_instance_exited("t", "a");
        assert!(e.on_instance_running("t", "c", 5000, 1, 4).evict.is_empty());
    }

    #[test]
    fn exit_reduces_usage() {
        let mut e = QuotaEnforcer::new(cfg(10_000, 1_000_000));
        e.on_instance_running("t", "i1", 6000, 1, 1);
        e.on_instance_exited("t", "i1");
        // After exit, a new 6000 instance fits (usage was reset).
        let out = e.on_instance_running("t", "i2", 6000, 1, 2);
        assert!(out.evict.is_empty());
    }

    #[test]
    fn mem_over_quota_triggers_eviction() {
        let mut e = QuotaEnforcer::new(cfg(1_000_000, 8192));
        e.on_instance_running("t", "i1", 1, 5000, 1);
        let out = e.on_instance_running("t", "i2", 1, 5000, 2);
        assert_eq!(out.evict, vec!["i2".to_string()]);
        assert_eq!(out.cooldown_ms, Some(7000));
    }
}
