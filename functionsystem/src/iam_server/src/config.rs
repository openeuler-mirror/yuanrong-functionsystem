use std::time::Duration;

use clap::builder::BoolishValueParser;
use clap::Parser;
use clap::ValueEnum;
use yr_common::error::{YrError, YrResult};
use yr_common::etcd_keys::with_prefix;

/// How this process participates in leader election for mutating IAM operations.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, ValueEnum)]
pub enum ElectionMode {
    /// No distributed election; every instance may perform writes (single-replica deployments).
    #[default]
    Standalone,
    /// etcd v3 election (`Campaign`) with lease keep-alive.
    Etcd,
    /// etcd KV `Txn` with `create_revision == 0` and lease TTL (portable vs campaign API).
    Txn,
    /// Same as etcd (typical HA layout: IAM pods share an etcd cluster for coordination).
    K8s,
}

/// Which credential families are enabled.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, ValueEnum)]
pub enum IamCredentialType {
    Token,
    #[value(alias = "ak_sk")]
    AkSk,
    #[default]
    Both,
}

/// C++ `install_iam_server` extras (TLS, IdPs, tracing); accepted, ignored.
#[derive(Parser, Debug, Default)]
#[allow(dead_code)]
pub struct IamCppIgnored {
    #[arg(long = "log_config", default_value = "")]
    pub log_config: String,
    #[arg(long = "enable_trace", default_value = "")]
    pub enable_trace: String,
    #[arg(long = "ssl_enable", default_value = "")]
    pub ssl_enable: String,
    #[arg(long = "ssl_base_path", default_value = "")]
    pub ssl_base_path: String,
    #[arg(long = "ssl_root_file", default_value = "")]
    pub ssl_root_file: String,
    #[arg(long = "ssl_cert_file", default_value = "")]
    pub ssl_cert_file: String,
    #[arg(long = "ssl_key_file", default_value = "")]
    pub ssl_key_file: String,
    #[arg(long = "auth_provider", default_value = "")]
    pub auth_provider: String,
    #[arg(long = "keycloak_enabled", default_value = "")]
    pub keycloak_enabled: String,
    #[arg(long = "keycloak_url", default_value = "")]
    pub keycloak_url: String,
    #[arg(long = "keycloak_issuer_url", default_value = "")]
    pub keycloak_issuer_url: String,
    #[arg(long = "keycloak_realm", default_value = "")]
    pub keycloak_realm: String,
    #[arg(long = "casdoor_enabled", default_value = "")]
    pub casdoor_enabled: String,
    #[arg(long = "casdoor_endpoint", default_value = "")]
    pub casdoor_endpoint: String,
    #[arg(long = "casdoor_public_endpoint", default_value = "")]
    pub casdoor_public_endpoint: String,
    #[arg(long = "casdoor_client_id", default_value = "")]
    pub casdoor_client_id: String,
    #[arg(long = "casdoor_client_secret", default_value = "")]
    pub casdoor_client_secret: String,
    #[arg(long = "casdoor_organization", default_value = "")]
    pub casdoor_organization: String,
    #[arg(long = "casdoor_application", default_value = "")]
    pub casdoor_application: String,
    #[arg(long = "casdoor_admin_user", default_value = "")]
    pub casdoor_admin_user: String,
    #[arg(long = "casdoor_admin_password", default_value = "")]
    pub casdoor_admin_password: String,
    #[arg(long = "casdoor_jwt_public_key", default_value = "")]
    pub casdoor_jwt_public_key: String,
}

#[derive(Parser, Debug)]
#[command(name = "iam_server", about = "openYuanrong IAM Server (Rust)")]
pub struct CliArgs {
    #[arg(
        long = "ip",
        default_value = "0.0.0.0",
        visible_alias = "iam_server_host",
        aliases = ["host", "iam-server-host"]
    )]
    pub host: String,

    #[arg(
        long = "http_listen_port",
        default_value_t = 8300,
        visible_alias = "iam_server_port",
        aliases = ["port", "http-listen-port", "iam-server-port"]
    )]
    pub port: u16,

    /// C++ passes etcd URLs here when using external meta store; same as `etcd_address`.
    #[arg(long = "meta_store_address", default_value = "", aliases = ["meta-store-address"])]
    pub meta_store_address: String,

    /// Comma-separated etcd endpoints, e.g. `127.0.0.1:2379`
    #[arg(
        long = "etcd_address",
        value_delimiter = ',',
        aliases = ["etcd-endpoints"]
    )]
    pub etcd_endpoints: Vec<String>,

    #[arg(
        long = "cluster_id",
        default_value = "default",
        visible_alias = "cluster_name",
        aliases = ["cluster-id", "cluster-name"]
    )]
    pub cluster_id: String,

    #[arg(
        long = "enable_iam",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable_internal_iam",
        aliases = ["enable-iam", "enable-internal-iam"]
    )]
    pub enable_iam: bool,

    /// Default token time-to-live (seconds) when `X-TTL` is not provided.
    #[arg(
        long = "token_expired_time_span",
        visible_alias = "token_ttl_seconds",
        default_value_t = 3600,
        aliases = ["token-expired-time-span", "token-ttl-seconds"]
    )]
    pub token_expired_time_span: u64,

    #[arg(
        long = "election_mode",
        visible_alias = "iam_election_mode",
        value_enum,
        default_value_t = ElectionMode::Standalone,
        aliases = ["election-mode", "iam-election-mode"]
    )]
    pub election_mode: ElectionMode,

    #[arg(
        long = "iam_credential_type",
        visible_alias = "credential_type",
        value_enum,
        default_value_t = IamCredentialType::Token,
        aliases = ["iam-credential-type", "credential-type"]
    )]
    pub iam_credential_type: IamCredentialType,

    /// Physical etcd key = this prefix concatenated with the logical key (C++ `KvClientStrategy::GetKeyWithPrefix`).
    #[arg(
        long = "etcd_table_prefix",
        default_value = "",
        visible_alias = "meta_store_prefix",
        aliases = ["etcd-table-prefix", "meta-store-prefix"]
    )]
    pub etcd_table_prefix: String,

    /// Extra secret mixed into the HMAC signing key (set in production).
    #[arg(
        long = "iam_signing_secret",
        default_value = "",
        visible_alias = "iam_hmac_secret",
        aliases = ["iam-signing-secret", "iam-hmac-secret"]
    )]
    pub iam_signing_secret: String,

    /// C++ `node_id`; used as `instance_id` when `--instance-id` is omitted.
    #[arg(long = "node_id", default_value = "", aliases = ["node-id"])]
    pub node_id: String,

    /// Unique id for this instance in etcd election (defaults to hostname or `node_id`).
    #[arg(long = "instance_id", aliases = ["instance-id"])]
    pub instance_id: Option<String>,

    #[command(flatten)]
    pub cpp_ignored: IamCppIgnored,
}

#[derive(Debug, Clone)]
pub struct IamConfig {
    pub host: String,
    pub port: u16,
    pub etcd_endpoints: Vec<String>,
    pub cluster_id: String,
    pub enable_iam: bool,
    pub token_ttl_default: Duration,
    pub election_mode: ElectionMode,
    pub iam_credential_type: IamCredentialType,
    pub etcd_table_prefix: String,
    pub iam_signing_secret: String,
    pub instance_id: String,
}

impl IamConfig {
    pub fn from_cli(args: CliArgs) -> YrResult<Self> {
        let mut etcd_endpoints = args.etcd_endpoints.clone();
        if etcd_endpoints.is_empty() {
            let m = args.meta_store_address.trim();
            if !m.is_empty() {
                etcd_endpoints = m
                    .split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect();
            }
        }

        let instance_id = if let Some(id) = args.instance_id.clone() {
            id
        } else if !args.node_id.trim().is_empty() {
            args.node_id.clone()
        } else {
            std::env::var("HOSTNAME").unwrap_or_else(|_| uuid::Uuid::new_v4().to_string())
        };

        Ok(Self {
            host: args.host,
            port: args.port,
            etcd_endpoints,
            cluster_id: args.cluster_id,
            enable_iam: args.enable_iam,
            token_ttl_default: Duration::from_secs(args.token_expired_time_span),
            election_mode: args.election_mode,
            iam_credential_type: args.iam_credential_type,
            etcd_table_prefix: args.etcd_table_prefix,
            iam_signing_secret: args.iam_signing_secret,
            instance_id,
        })
    }

    /// Physical etcd key for `ElectionMode::Txn` (`{prefix}/yr/election/yr-iam`).
    pub fn txn_election_key(&self) -> String {
        let p = self.etcd_table_prefix.trim_end_matches('/');
        with_prefix(p, "/yr/election/yr-iam")
    }

    pub fn validate(&self) -> YrResult<()> {
        if self.enable_iam && self.etcd_endpoints.is_empty() {
            return Err(YrError::Config(
                "etcd_endpoints is required when enable_iam is true".into(),
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
