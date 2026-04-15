use std::time::Duration;

use clap::Parser;
use yr_common::error::YrError;
use yr_iam::config::{
    CliArgs, ElectionMode, IamConfig, IamCredentialType,
};

#[test]
fn cli_defaults_match_expected() {
    let cli = CliArgs::try_parse_from(["yr-iam"]).unwrap();
    assert_eq!(cli.host, "0.0.0.0");
    assert_eq!(cli.port, 8300);
    assert!(cli.etcd_endpoints.is_empty());
    assert_eq!(cli.cluster_id, "default");
    assert!(cli.enable_iam);
    assert_eq!(cli.token_expired_time_span, 3600);
    assert_eq!(cli.election_mode, ElectionMode::Standalone);
    assert_eq!(cli.iam_credential_type, IamCredentialType::Token);
    assert_eq!(cli.etcd_table_prefix, "");
    assert_eq!(cli.iam_signing_secret, "");
    assert!(cli.instance_id.is_none());
}

#[test]
fn from_cli_maps_explicit_fields_and_ttl() {
    let cli = CliArgs::try_parse_from([
        "yr-iam",
        "--host",
        "10.0.0.1",
        "--port",
        "9300",
        "--etcd-endpoints",
        "127.0.0.1:2379,127.0.0.1:2479",
        "--cluster-id",
        "c1",
        "--token-expired-time-span",
        "120",
        "--etcd-table-prefix",
        "/prefix/",
        "--iam-signing-secret",
        "s3cr3t",
        "--instance-id",
        "pod-7",
    ])
    .unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.host, "10.0.0.1");
    assert_eq!(cfg.port, 9300);
    assert_eq!(
        cfg.etcd_endpoints,
        vec!["127.0.0.1:2379".to_string(), "127.0.0.1:2479".to_string()]
    );
    assert_eq!(cfg.cluster_id, "c1");
    assert!(cfg.enable_iam);
    assert_eq!(cfg.token_ttl_default, Duration::from_secs(120));
    assert_eq!(cfg.etcd_table_prefix, "/prefix/");
    assert_eq!(cfg.iam_signing_secret, "s3cr3t");
    assert_eq!(cfg.instance_id, "pod-7");
}

#[test]
fn election_mode_etcd_from_cli() {
    let cli = CliArgs::try_parse_from(["yr-iam", "--election-mode", "etcd"]).unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.election_mode, ElectionMode::Etcd);
}

#[test]
fn election_mode_txn_from_cli() {
    let cli = CliArgs::try_parse_from(["yr-iam", "--election-mode", "txn"]).unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.election_mode, ElectionMode::Txn);
}

#[test]
fn election_mode_k8s_from_cli() {
    let cli = CliArgs::try_parse_from(["yr-iam", "--election-mode", "k8s"]).unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.election_mode, ElectionMode::K8s);
}

#[test]
fn credential_type_variants_from_cli() {
    let cli = CliArgs::try_parse_from(["yr-iam", "--iam-credential-type", "ak-sk"]).unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.iam_credential_type, IamCredentialType::AkSk);

    let cli = CliArgs::try_parse_from(["yr-iam", "--iam-credential-type", "both"]).unwrap();
    let cfg = IamConfig::from_cli(cli).unwrap();
    assert_eq!(cfg.iam_credential_type, IamCredentialType::Both);
}

#[test]
fn validate_requires_etcd_when_iam_enabled() {
    let mut cfg = sample_config();
    cfg.enable_iam = true;
    cfg.etcd_endpoints.clear();
    let err = cfg.validate().unwrap_err();
    assert!(matches!(err, YrError::Config(_)));
}

#[test]
fn validate_requires_etcd_for_distributed_election() {
    let mut cfg = sample_config();
    cfg.enable_iam = false;
    cfg.etcd_endpoints.clear();
    cfg.election_mode = ElectionMode::Etcd;
    let err = cfg.validate().unwrap_err();
    assert!(matches!(err, YrError::Config(_)));
}

#[test]
fn validate_ok_when_iam_off_and_no_etcd() {
    let mut cfg = sample_config();
    cfg.enable_iam = false;
    cfg.etcd_endpoints.clear();
    cfg.election_mode = ElectionMode::Standalone;
    cfg.validate().unwrap();
}

#[test]
fn txn_election_key_trims_prefix_slash() {
    let mut cfg = sample_config();
    cfg.etcd_table_prefix = "/my/prefix//".to_string();
    assert_eq!(cfg.txn_election_key(), "/my/prefix/yr/election/yr-iam");

    cfg.etcd_table_prefix = String::new();
    assert_eq!(cfg.txn_election_key(), "/yr/election/yr-iam");
}

fn sample_config() -> IamConfig {
    IamConfig {
        host: "0.0.0.0".into(),
        port: 8300,
        etcd_endpoints: vec!["127.0.0.1:2379".into()],
        cluster_id: "c".into(),
        enable_iam: true,
        token_ttl_default: Duration::from_secs(3600),
        election_mode: ElectionMode::Standalone,
        iam_credential_type: IamCredentialType::Token,
        etcd_table_prefix: String::new(),
        iam_signing_secret: String::new(),
        instance_id: "id".into(),
    }
}
