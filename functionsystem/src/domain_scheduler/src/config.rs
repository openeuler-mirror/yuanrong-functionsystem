use clap::builder::BoolishValueParser;
use clap::Parser;
use clap::ValueEnum;
use yr_common::error::{YrError, YrResult};
use yr_common::etcd_keys::{with_prefix, SCHEDULER_TOPOLOGY, YR_DOMAIN_SCHEDULER_PREFIX, YR_MASTER_PREFIX};

/// Leader election backend (same semantics as yr-master).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, ValueEnum)]
pub enum ElectionMode {
    #[default]
    Standalone,
    Etcd,
    Txn,
    K8s,
}

#[derive(Parser, Debug)]
#[command(
    name = "yr-domain-scheduler",
    about = "openYuanrong domain scheduler (function_master → locals)"
)]
pub struct CliArgs {
    #[arg(long, default_value = "0.0.0.0")]
    pub host: String,

    /// gRPC listen port (DomainSchedulerService).
    #[arg(long, default_value_t = 8401)]
    pub port: u16,

    /// HTTP listen port (health / metrics-style API).
    #[arg(long, default_value_t = 8481)]
    pub http_port: u16,

    /// Global scheduler gRPC address, e.g. `127.0.0.1:8400`. Empty skips registration.
    #[arg(long, default_value = "")]
    pub global_scheduler_address: String,

    /// Comma-separated etcd endpoints.
    #[arg(long, value_delimiter = ',')]
    pub etcd_endpoints: Vec<String>,

    /// Prepended to logical etcd paths (no trailing slash).
    #[arg(long, default_value = "")]
    pub etcd_table_prefix: String,

    /// Stable id for this domain scheduler instance (used in election path and registration).
    #[arg(long, default_value = "domain-0")]
    pub node_id: String,

    #[arg(long, value_enum, default_value_t = ElectionMode::Standalone)]
    pub election_mode: ElectionMode,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new()
    )]
    pub enable_preemption: bool,

    #[arg(long, default_value_t = 100)]
    pub max_priority: i32,

    /// Heartbeat staleness / housekeeping interval (ms).
    #[arg(long, default_value_t = 5000)]
    pub pull_resource_interval_ms: u64,
}

#[derive(Debug, Clone)]
pub struct DomainSchedulerConfig {
    pub host: String,
    pub port: u16,
    pub http_port: u16,
    pub global_scheduler_address: String,
    pub etcd_endpoints: Vec<String>,
    pub etcd_table_prefix: String,
    pub node_id: String,
    pub election_mode: ElectionMode,
    pub enable_preemption: bool,
    pub max_priority: i32,
    pub pull_resource_interval_ms: u64,
    /// Process identity for etcd campaign (HOSTNAME or random UUID).
    pub instance_id: String,
}

impl DomainSchedulerConfig {
    pub fn from_cli(args: CliArgs) -> YrResult<Self> {
        let instance_id =
            std::env::var("HOSTNAME").unwrap_or_else(|_| uuid::Uuid::new_v4().to_string());
        Ok(Self {
            host: args.host,
            port: args.port,
            http_port: args.http_port,
            global_scheduler_address: args.global_scheduler_address,
            etcd_endpoints: args.etcd_endpoints,
            etcd_table_prefix: args.etcd_table_prefix,
            node_id: args.node_id,
            election_mode: args.election_mode,
            enable_preemption: args.enable_preemption,
            max_priority: args.max_priority,
            pull_resource_interval_ms: args.pull_resource_interval_ms,
            instance_id,
        })
    }

    pub fn grpc_listen_addr(&self) -> String {
        format!("{}:{}", self.host, self.port)
    }

    pub fn advertise_grpc_addr(&self) -> String {
        self.grpc_listen_addr()
    }

    /// Base path for domain scheduler keys: `etcd_table_prefix` + [`YR_DOMAIN_SCHEDULER_PREFIX`].
    pub fn domain_key_base(&self) -> String {
        let p = self.etcd_table_prefix.trim_end_matches('/');
        with_prefix(p, YR_DOMAIN_SCHEDULER_PREFIX)
    }

    pub fn election_name(&self) -> Vec<u8> {
        format!(
            "{}/leader_election/{}",
            self.domain_key_base(),
            self.node_id
        )
        .into_bytes()
    }

    /// Physical etcd key for `ElectionMode::Txn` (`{domain_base}/election/{node_id}`).
    pub fn txn_election_key(&self) -> String {
        format!("{}/election/{}", self.domain_key_base(), self.node_id)
    }

    /// Topology snapshot key written by yr-master (`SCHEDULER_TOPOLOGY` under master base).
    pub fn master_topology_key(&self) -> String {
        let p = self.etcd_table_prefix.trim_end_matches('/');
        let base = with_prefix(p, YR_MASTER_PREFIX);
        format!("{base}{}", SCHEDULER_TOPOLOGY)
    }

    pub fn validate(&self) -> YrResult<()> {
        if matches!(
            self.election_mode,
            ElectionMode::Etcd | ElectionMode::Txn | ElectionMode::K8s
        ) && self.etcd_endpoints.is_empty()
        {
            return Err(YrError::Config(
                "etcd_endpoints is required for etcd, txn, or k8s election_mode".into(),
            ));
        }
        if self.max_priority < 1 {
            return Err(YrError::Config("max_priority must be >= 1".into()));
        }
        Ok(())
    }
}
