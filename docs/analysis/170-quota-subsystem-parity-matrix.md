# Quota / Tenant-Cooldown Subsystem Parity Matrix

Date: 2026-06-06
Branch: `rust-rewrite`
Oracle: `feature/sandbox` C++ `QuotaManagerActor` subsystem
Spec: C++ `docs/superpowers/specs/2026-03-12-quota-manager-design.md`

## Scope

Depth-verification of the first "has-impl, depth-unverified" subsystem from doc 169:
tenant resource quota + cooldown.

## C++ design (oracle)

`function_master/instance_manager/quota_manager/`:
- **QuotaConfig** (`quota_config.h`): loads JSON via `--quota_config_file`; `TenantQuota{cpuMillicores, memLimitMb, cooldownMs=10000}`; `GetQuota(tenant)` = perTenant → default. Flag unset → built-in default + WARNING; flag set but bad file → FATAL.
- **QuotaManagerActor**: tracks per-tenant usage from instance RUNNING/EXITED events; on over-quota → LIFO-evict newest instances (ForwardKill) until under quota; sends `TenantQuotaExceeded{tenantID, cooldownMs}`.
- **DomainSchedSrvActor** forwards `TenantQuotaExceeded` → **InstanceCtrlActor** which keeps `blockedTenants_` + cooldown Timer; `Schedule()` front rejects blocked tenants with `ERR_RESOURCE_NOT_ENOUGH`; Timer expiry erases the tenant.
- Enforcement disabled when config path empty.

## Rust status (per component)

| C++ component / behavior | Rust | Status |
| --- | --- | --- |
| `Schedule()` rejects blocked tenants | `instance_ctrl.rs::tenant_cooldown_active` checked in `local_scheduler.rs::schedule_local` → returns `ErrCreateRateLimited` | **Present** (consumer side) |
| Cooldown state + timer / expiry | `common/utils/tenant_cooldown.rs::TenantCooldownManager` (+ `Drop`); `set_tenant_cooldown_ms` | **Present** (consumer side) |
| `TenantQuotaExceeded` proto | `proto/posix/message.proto:1148` | **Present** (wire) |
| `--quota_config_file` flag | `config.rs:174` + `cli_compat.rs` | **Accepted but IGNORED** — no loader reads it |
| **QuotaConfig JSON load / GetQuota** | — | **GAP** — no JSON parse, no TenantQuota struct, no default/per-tenant lookup |
| **Per-tenant usage tracking** (RUNNING/EXITED accounting) | — | **GAP** |
| **Over-quota detection + LIFO eviction** | — | **GAP** (QuotaManagerActor core) |
| **Cooldown PRODUCER** (`set_tenant_cooldown_ms` driven by enforcement) | only called from `instance_lifecycle_test.rs:194` | **GAP** — no real producer; cooldown can never trigger in production |

`iam_server/routes.rs::tenant_quota` is a separate IAM/Keycloak quota-polling endpoint, not this subsystem.

## Conclusion

Quota is a **surface implementation**: the cooldown CONSUMER (schedule rejection + timer) and the
proto wire are present and correct, but the **enforcement CORE is absent** — config is never loaded,
usage is never tracked, over-quota is never detected, eviction never happens, and the cooldown is
never triggered outside tests. In production the `--quota_config_file` flag is silently ignored and
tenant quotas are unenforced.

This converts doc-169's "quota: has impl, depth unverified" into **"consumer+wire present;
enforcement core is a confirmed gap."**

## Closure plan

1. **QuotaConfig** (testable, low-risk): `TenantQuota{cpu_millicores, mem_limit_mb, cooldown_ms}`,
   JSON loader for `--quota_config_file` (empty path → disabled + warn; bad file → hard error),
   `quota_for_tenant()` = per-tenant → default. Unit tests. ← start here.
2. **Enforcement decision** (pure, testable): given per-tenant usage + quota + sorted-by-create-time
   instances, compute over-quota and the LIFO eviction set + cooldown_ms. Unit tests.
3. **Wiring** (needs cross-component, harder to unit-test): drive usage from function_master instance
   state changes; perform eviction via the kill path; deliver cooldown to the proxy
   (`set_tenant_cooldown_ms`). Verify against C++ flow + add an e2e cooldown test.

Steps 1–2 are bounded and unit-testable now; step 3 is the cross-actor integration.

## Progress (2026-06-06)

- **Steps 1–2 DONE**: `function_master/src/quota.rs` adds `TenantQuota` (C++ default
  32000/65536/10000), `QuotaConfig::{load_from_file, quota_for_tenant, disabled}` (JSON
  with `cpuMillicores`/`memMb`/`cooldownMs`, default+per-tenant; empty path → disabled,
  bad file → error), and `QuotaEnforcer` (per-tenant usage tracking + `check_and_enforce`
  LIFO eviction + cooldown). 9 unit tests pass; yr-master lib 14/14 green.
- **Step 3 STILL OPEN**: wire `QuotaConfig::load_from_file(config.quota_config_file)` at
  master startup; drive `QuotaEnforcer::on_instance_running/exited` from master instance
  state changes; perform eviction via the kill path; deliver `cooldown_ms` to the proxy
  (`InstanceController::set_tenant_cooldown_ms`) via the `TenantQuotaExceeded` flow; add an
  e2e cooldown test. Until step 3, the flag is loaded-capable but not yet driving runtime.
