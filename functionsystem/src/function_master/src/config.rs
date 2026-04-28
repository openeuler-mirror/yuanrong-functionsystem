use clap::builder::BoolishValueParser;
use clap::Parser;
use clap::ValueEnum;
use yr_common::error::{YrError, YrResult};

/// Parse C++ `--ip=host:port` (IPv4 `a:b:c:d:port` uses last colon; or `[ipv6]:port`).
pub fn parse_cpp_listen(s: &str) -> Option<(String, u16)> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    if s.starts_with('[') {
        if let Some(end) = s.find(']') {
            let host = s[..=end].to_string();
            let rest = s[end + 1..].trim_start();
            if let Some(p) = rest.strip_prefix(':') {
                let port: u16 = p.parse().ok()?;
                return Some((host, port));
            }
        }
        return None;
    }
    let idx = s.rfind(':')?;
    let host = s[..idx].trim().to_string();
    if host.is_empty() {
        return None;
    }
    let port: u16 = s[idx + 1..].trim().parse().ok()?;
    Some((host, port))
}

fn election_mode_from_cpp(s: &str) -> Result<ElectionMode, YrError> {
    let t = s.trim().to_ascii_lowercase();
    Ok(match t.as_str() {
        "" | "standalone" | "0" => ElectionMode::Standalone,
        "etcd" | "1" => ElectionMode::Etcd,
        "txn" | "2" => ElectionMode::Txn,
        "k8s" | "k8" | "3" => ElectionMode::K8s,
        _ => {
            return Err(YrError::Config(format!(
                "invalid election_mode {s:?} (expected standalone|etcd|txn|k8s or 0-3)"
            )));
        }
    })
}

fn parse_election_cli(s: &str) -> Result<ElectionMode, String> {
    election_mode_from_cpp(s).map_err(|e| e.to_string())
}

/// Flags present on C++ `function_master` that Rust does not implement yet; accepted so `install.sh` parses.
#[derive(Parser, Debug)]
#[allow(dead_code)]
pub struct MasterCppIgnored {
    #[arg(long = "log_config", default_value = "")]
    pub log_config: String,
    #[arg(long = "sys_func_retry_period", default_value = "")]
    pub sys_func_retry_period: String,
    #[arg(long = "litebus_thread_num", default_value = "")]
    pub litebus_thread_num: String,
    #[arg(long = "system_timeout", default_value = "")]
    pub system_timeout: String,
    #[arg(long = "enable_metrics", default_value = "")]
    pub enable_metrics: String,
    #[arg(long = "metrics_config", default_value = "")]
    pub metrics_config: String,
    #[arg(long = "metrics_config_file", default_value = "")]
    pub metrics_config_file: String,
    #[arg(long = "pull_resource_interval", default_value = "")]
    pub pull_resource_interval: String,
    #[arg(long = "enable_print_resource_view", default_value = "")]
    pub enable_print_resource_view: String,
    #[arg(long = "schedule_relaxed", default_value = "")]
    pub schedule_relaxed: String,
    #[arg(long = "enable_preemption", default_value = "")]
    pub enable_preemption: String,
    #[arg(long = "meta_store_excluded_keys", default_value = "")]
    pub meta_store_excluded_keys: String,
    #[arg(long = "ssl_enable", default_value = "")]
    pub ssl_enable: String,
    #[arg(long = "ssl_base_path", default_value = "")]
    pub ssl_base_path: String,
    #[arg(long = "etcd_auth_type", default_value = "")]
    pub etcd_auth_type: String,
    #[arg(long = "etcd_root_ca_file", default_value = "")]
    pub etcd_root_ca_file: String,
    #[arg(long = "etcd_cert_file", default_value = "")]
    pub etcd_cert_file: String,
    #[arg(long = "etcd_key_file", default_value = "")]
    pub etcd_key_file: String,
    #[arg(long = "etcd_ssl_base_path", default_value = "")]
    pub etcd_ssl_base_path: String,
    #[arg(long = "etcd_target_name_override", default_value = "")]
    pub etcd_target_name_override: String,
    #[arg(long = "ssl_root_file", default_value = "")]
    pub ssl_root_file: String,
    #[arg(long = "ssl_cert_file", default_value = "")]
    pub ssl_cert_file: String,
    #[arg(long = "ssl_key_file", default_value = "")]
    pub ssl_key_file: String,
    #[arg(long = "metrics_ssl_enable", default_value = "")]
    pub metrics_ssl_enable: String,
    #[arg(long = "enable_trace", default_value = "")]
    pub enable_trace: String,
    #[arg(long = "trace_config", default_value = "")]
    pub trace_config: String,
}
use yr_common::etcd_keys::{
    explorer, with_prefix, INSTANCE_PATH_PREFIX, READY_AGENT_CNT_KEY, SCHEDULER_TOPOLOGY,
};
use yr_common::etcd_keys::{
    ABNORMAL_SCHEDULER_PREFIX, BUSPROXY_PATH_PREFIX, FUNC_META_PATH_PREFIX,
};

/// Leader election backend (k8s uses the same etcd campaign path as etcd mode).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, ValueEnum)]
pub enum ElectionMode {
    #[default]
    Standalone,
    Etcd,
    /// Leader election via etcd KV `Txn` (compare-and-swap on `create_revision == 0`) and lease TTL.
    Txn,
    K8s,
}

/// How locals are assigned to domain scheduler slots.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, ValueEnum)]
pub enum AssignmentStrategy {
    #[default]
    LeastLoaded,
    RoundRobin,
}

#[derive(Parser, Debug)]
#[command(
    name = "function_master",
    about = "openYuanrong global scheduler / function_master (Rust)"
)]
pub struct CliArgs {
    /// C++ combined gRPC listen `host:port` (overrides `host` + `port` when set).
    #[arg(long = "ip")]
    pub ip: Option<String>,

    #[arg(long = "host", default_value = "0.0.0.0")]
    pub host: String,

    /// gRPC listen port when `ip` is not used (GlobalSchedulerService).
    #[arg(long = "port", default_value_t = 8400)]
    pub port: u16,

    #[arg(
        long = "http_port",
        default_value_t = 8480,
        visible_alias = "http-port"
    )]
    pub http_port: u16,

    /// Comma-separated etcd endpoints (C++ `etcd_address`).
    #[arg(
        long = "etcd_address",
        visible_alias = "etcd-endpoints",
        value_delimiter = ','
    )]
    pub etcd_endpoints: Vec<String>,

    #[arg(
        long = "etcd_table_prefix",
        default_value = "",
        alias = "etcd-table-prefix"
    )]
    pub etcd_table_prefix: String,

    #[arg(
        long = "cluster_id",
        default_value = "default",
        visible_alias = "cluster-id"
    )]
    pub cluster_id: String,

    #[arg(
        long = "election_mode",
        default_value = "standalone",
        value_parser = parse_election_cli,
        visible_alias = "election-mode"
    )]
    pub election_mode: ElectionMode,

    #[arg(
        long = "max_locals_per_domain",
        default_value_t = 64,
        alias = "max-locals-per-domain"
    )]
    pub max_locals_per_domain: u32,

    #[arg(
        long = "max_domain_sched_per_domain",
        default_value_t = 1000,
        alias = "max-domain-sched-per-domain"
    )]
    pub max_domain_sched_per_domain: u32,

    #[arg(
        long = "schedule_retry_sec",
        default_value_t = 10,
        alias = "schedule-retry-sec"
    )]
    pub schedule_retry_sec: u64,

    #[arg(
        long = "domain_schedule_timeout_ms",
        default_value_t = 5000,
        alias = "domain-schedule-timeout-ms"
    )]
    pub domain_schedule_timeout_ms: u64,

    #[arg(
        long = "enable_meta_store",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-meta-store"
    )]
    pub enable_meta_store: bool,

    #[arg(
        long = "meta_store_address",
        default_value = "",
        alias = "meta-store-address"
    )]
    pub meta_store_address: String,

    #[arg(
        long = "meta_store_port",
        default_value_t = 2389,
        alias = "meta-store-port"
    )]
    pub meta_store_port: u16,

    #[arg(
        long = "assignment_strategy",
        value_enum,
        default_value_t = AssignmentStrategy::LeastLoaded,
        visible_alias = "assignment-strategy"
    )]
    pub assignment_strategy: AssignmentStrategy,

    #[arg(
        long = "default_domain_address",
        default_value = "127.0.0.1:8401",
        alias = "default-domain-address"
    )]
    pub default_domain_address: String,

    #[arg(long = "node_id", default_value = "", alias = "node-id")]
    pub node_id: String,

    #[arg(
        long = "enable_persistence",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        alias = "enable-persistence"
    )]
    pub enable_persistence: bool,

    #[arg(
        long = "runtime_recover_enable",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        alias = "runtime-recover-enable"
    )]
    pub runtime_recover_enable: bool,

    #[arg(
        long = "is_schedule_tolerate_abnormal",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        alias = "is-schedule-tolerate-abnormal"
    )]
    pub is_schedule_tolerate_abnormal: bool,

    #[arg(
        long = "decrypt_algorithm",
        default_value = "NO_CRYPTO",
        visible_alias = "decrypt-algorithm"
    )]
    pub decrypt_algorithm: String,

    #[arg(
        long = "schedule_plugins",
        default_value = "",
        alias = "schedule-plugins"
    )]
    pub schedule_plugins: String,

    #[arg(
        long = "aggregated_schedule_strategy",
        default_value = "no_aggregate",
        alias = "aggregated-schedule-strategy"
    )]
    pub aggregated_schedule_strategy: String,

    /// C++ `max_priority` (scheduler tier count).
    #[arg(
        long = "max_priority",
        default_value_t = 16,
        aliases = ["sched_max_priority", "sched-max-priority"]
    )]
    pub sched_max_priority: u16,

    #[arg(
        long = "migrate_enable",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "migrate-enable"
    )]
    pub migrate_enable: bool,

    #[arg(
        long = "grace_period_seconds",
        default_value_t = 25,
        visible_alias = "grace-period-seconds"
    )]
    pub grace_period_seconds: u32,

    #[arg(
        long = "health_monitor_max_failure",
        default_value_t = 5,
        visible_alias = "health-monitor-max-failure"
    )]
    pub health_monitor_max_failure: u32,

    #[arg(
        long = "health_monitor_retry_interval",
        default_value_t = 3000,
        visible_alias = "health-monitor-retry-interval"
    )]
    pub health_monitor_retry_interval: u32,

    #[arg(
        long = "enable_horizontal_scale",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-horizontal-scale"
    )]
    pub enable_horizontal_scale: bool,

    #[arg(
        long = "pool_config_path",
        default_value = "",
        visible_alias = "pool-config-path"
    )]
    pub pool_config_path: String,

    #[arg(
        long = "domain_heartbeat_timeout",
        default_value_t = 6000,
        visible_alias = "domain-heartbeat-timeout"
    )]
    pub domain_heartbeat_timeout: u32,

    #[arg(
        long = "system_tenant_id",
        default_value = "0",
        visible_alias = "system-tenant-id"
    )]
    pub system_tenant_id: String,

    #[arg(
        long = "services_path",
        default_value = "/",
        visible_alias = "services-path"
    )]
    pub services_path: String,

    #[arg(long = "lib_path", default_value = "/", visible_alias = "lib-path")]
    pub lib_path: String,

    #[arg(
        long = "function_meta_path",
        default_value = "/home/sn/function-metas",
        visible_alias = "function-meta-path"
    )]
    pub function_meta_path: String,

    #[arg(
        long = "enable_sync_sys_func",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-sync-sys-func"
    )]
    pub enable_sync_sys_func: bool,

    #[arg(
        long = "meta_store_mode",
        default_value = "local",
        visible_alias = "meta-store-mode"
    )]
    pub meta_store_mode: String,

    #[arg(
        long = "meta_store_max_flush_concurrency",
        default_value_t = 100,
        visible_alias = "meta-store-max-flush-concurrency"
    )]
    pub meta_store_max_flush_concurrency: u32,

    #[arg(
        long = "meta_store_max_flush_batch_size",
        default_value_t = 50,
        visible_alias = "meta-store-max-flush-batch-size"
    )]
    pub meta_store_max_flush_batch_size: u32,

    #[command(flatten)]
    pub cpp_ignored: MasterCppIgnored,
}

#[derive(Debug, Clone)]
pub struct MasterConfig {
    pub host: String,
    pub port: u16,
    pub http_port: u16,
    pub etcd_endpoints: Vec<String>,
    pub etcd_table_prefix: String,
    pub cluster_id: String,
    pub election_mode: ElectionMode,
    pub max_locals_per_domain: u32,
    pub max_domain_sched_per_domain: u32,
    pub schedule_retry_sec: u64,
    pub domain_schedule_timeout_ms: u64,
    pub enable_meta_store: bool,
    pub meta_store_address: String,
    pub meta_store_port: u16,
    pub assignment_strategy: AssignmentStrategy,
    pub default_domain_address: String,
    pub node_id: String,
    pub enable_persistence: bool,
    pub runtime_recover_enable: bool,
    pub is_schedule_tolerate_abnormal: bool,
    pub decrypt_algorithm: String,
    pub schedule_plugins: String,
    pub aggregated_schedule_strategy: String,
    pub sched_max_priority: u16,
    pub migrate_enable: bool,
    pub grace_period_seconds: u32,
    pub health_monitor_max_failure: u32,
    pub health_monitor_retry_interval: u32,
    pub enable_horizontal_scale: bool,
    pub pool_config_path: String,
    pub domain_heartbeat_timeout: u32,
    pub system_tenant_id: String,
    pub services_path: String,
    pub lib_path: String,
    pub function_meta_path: String,
    pub enable_sync_sys_func: bool,
    pub meta_store_mode: String,
    pub meta_store_max_flush_concurrency: u32,
    pub meta_store_max_flush_batch_size: u32,
    pub ssl_enable: String,
    pub metrics_ssl_enable: String,
    pub ssl_base_path: String,
    pub ssl_root_file: String,
    pub ssl_cert_file: String,
    pub ssl_key_file: String,
    pub instance_id: String,
}

impl MasterConfig {
    pub fn from_cli(args: CliArgs) -> YrResult<Self> {
        let instance_id =
            std::env::var("HOSTNAME").unwrap_or_else(|_| uuid::Uuid::new_v4().to_string());

        let (host, port) = if let Some(ref ip) = args.ip {
            parse_cpp_listen(ip).ok_or_else(|| {
                YrError::Config(format!(
                    "invalid --ip {ip:?} (expected host:port or [ipv6]:port)"
                ))
            })?
        } else {
            (args.host.clone(), args.port)
        };
        Ok(Self {
            host,
            port,
            http_port: args.http_port,
            etcd_endpoints: args.etcd_endpoints,
            etcd_table_prefix: args.etcd_table_prefix,
            cluster_id: args.cluster_id,
            election_mode: args.election_mode,
            max_locals_per_domain: args.max_locals_per_domain,
            max_domain_sched_per_domain: args.max_domain_sched_per_domain,
            schedule_retry_sec: args.schedule_retry_sec,
            domain_schedule_timeout_ms: args.domain_schedule_timeout_ms,
            enable_meta_store: args.enable_meta_store,
            meta_store_address: args.meta_store_address,
            meta_store_port: args.meta_store_port,
            assignment_strategy: args.assignment_strategy,
            default_domain_address: args.default_domain_address,
            node_id: args.node_id,
            enable_persistence: args.enable_persistence,
            runtime_recover_enable: args.runtime_recover_enable,
            is_schedule_tolerate_abnormal: args.is_schedule_tolerate_abnormal,
            decrypt_algorithm: args.decrypt_algorithm,
            schedule_plugins: args.schedule_plugins,
            aggregated_schedule_strategy: args.aggregated_schedule_strategy,
            sched_max_priority: args.sched_max_priority,
            migrate_enable: args.migrate_enable,
            grace_period_seconds: args.grace_period_seconds,
            health_monitor_max_failure: args.health_monitor_max_failure,
            health_monitor_retry_interval: args.health_monitor_retry_interval,
            enable_horizontal_scale: args.enable_horizontal_scale,
            pool_config_path: args.pool_config_path,
            domain_heartbeat_timeout: args.domain_heartbeat_timeout,
            system_tenant_id: args.system_tenant_id,
            services_path: args.services_path,
            lib_path: args.lib_path,
            function_meta_path: args.function_meta_path,
            enable_sync_sys_func: args.enable_sync_sys_func,
            meta_store_mode: args.meta_store_mode,
            meta_store_max_flush_concurrency: args.meta_store_max_flush_concurrency,
            meta_store_max_flush_batch_size: args.meta_store_max_flush_batch_size,
            ssl_enable: args.cpp_ignored.ssl_enable,
            metrics_ssl_enable: args.cpp_ignored.metrics_ssl_enable,
            ssl_base_path: args.cpp_ignored.ssl_base_path,
            ssl_root_file: args.cpp_ignored.ssl_root_file,
            ssl_cert_file: args.cpp_ignored.ssl_cert_file,
            ssl_key_file: args.cpp_ignored.ssl_key_file,
            instance_id,
        })
    }

    fn etcd_prefix(&self) -> &str {
        self.etcd_table_prefix.trim_end_matches('/')
    }

    /// Scheduler topology document; matches C++ `SCHEDULER_TOPOLOGY` + table prefix.
    pub fn topology_key(&self) -> String {
        with_prefix(self.etcd_prefix(), SCHEDULER_TOPOLOGY)
    }

    /// Logical etcd key for topology (use with [`yr_metastore_client::MetaStoreClient`] which applies `etcd_table_prefix`).
    pub fn topology_logical_key() -> &'static str {
        SCHEDULER_TOPOLOGY
    }

    /// Prefix watch for instance KV; matches C++ `INSTANCE_PATH_PREFIX` + table prefix.
    pub fn instance_prefix(&self) -> String {
        with_prefix(self.etcd_prefix(), INSTANCE_PATH_PREFIX)
    }

    pub fn func_meta_prefix(&self) -> String {
        with_prefix(self.etcd_prefix(), FUNC_META_PATH_PREFIX)
    }

    pub fn busproxy_prefix(&self) -> String {
        with_prefix(self.etcd_prefix(), BUSPROXY_PATH_PREFIX)
    }

    pub fn abnormal_scheduler_prefix(&self) -> String {
        with_prefix(self.etcd_prefix(), ABNORMAL_SCHEDULER_PREFIX)
    }

    /// Leader election campaign name; matches C++ `DEFAULT_MASTER_ELECTION_KEY` + table prefix.
    pub fn election_name(&self) -> Vec<u8> {
        with_prefix(self.etcd_prefix(), explorer::DEFAULT_MASTER_ELECTION_KEY).into_bytes()
    }

    /// Physical etcd key for `ElectionMode::Txn` (`{prefix}/yr/election/yr-master`).
    pub fn txn_election_key(&self) -> String {
        with_prefix(self.etcd_prefix(), "/yr/election/yr-master")
    }

    /// Ready-agent count key; matches C++ `READY_AGENT_CNT_KEY` + table prefix.
    pub fn ready_agent_count_key(&self) -> String {
        with_prefix(self.etcd_prefix(), READY_AGENT_CNT_KEY)
    }

    pub fn validate(&self) -> YrResult<()> {
        if self.enable_meta_store && self.etcd_endpoints.is_empty() {
            return Err(YrError::Config(
                "etcd_endpoints is required when enable_meta_store is true".into(),
            ));
        }
        if matches!(
            self.election_mode,
            ElectionMode::Etcd | ElectionMode::Txn | ElectionMode::K8s
        ) && self.etcd_endpoints.is_empty()
        {
            return Err(YrError::Config(
                "etcd_endpoints is required for etcd, txn, or k8s election_mode".into(),
            ));
        }
        Ok(())
    }
}
