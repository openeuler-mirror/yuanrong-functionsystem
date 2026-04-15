//! Integration tests for `CommonConfig` CLI parsing and `load_config` helpers.

use clap::Parser;
use std::fs;
use std::io::Write;
use std::path::PathBuf;

use yr_common::config::{load_config, load_config_from_json_str, load_config_from_yaml_str};
use yr_common::CommonConfig;

#[derive(Parser)]
#[command(name = "yr-test")]
struct TestCli {
    #[command(flatten)]
    common: CommonConfig,
}

fn defaults() -> CommonConfig {
    TestCli::try_parse_from(["yr-test"]).unwrap().common
}

#[test]
fn default_etcd_core_strings_empty() {
    let c = defaults();
    assert_eq!(c.etcd_address, "");
    assert_eq!(c.etcd_table_prefix, "");
    assert_eq!(c.cluster_id, "");
}

#[test]
fn default_etcd_auth_strings() {
    let c = defaults();
    assert_eq!(c.etcd_auth_type, "Noauth");
    assert_eq!(c.etcd_secret_name, "");
    assert_eq!(c.etcd_ssl_base_path, "/home/sn/resource/etcd");
    assert_eq!(c.etcd_root_ca_file, "");
    assert_eq!(c.etcd_cert_file, "");
    assert_eq!(c.etcd_key_file, "");
    assert_eq!(c.etcd_target_name_override, "");
}

#[test]
fn default_ssl_flags_false() {
    let c = defaults();
    assert!(!c.ssl_enable);
    assert!(!c.ssl_downgrade_enable);
}

#[test]
fn default_ssl_paths() {
    let c = defaults();
    assert_eq!(c.ssl_base_path, "/");
    assert_eq!(c.ssl_root_file, "");
    assert_eq!(c.ssl_cert_file, "");
    assert_eq!(c.ssl_key_file, "");
}

#[test]
fn default_metrics_flags_and_strings() {
    let c = defaults();
    assert!(!c.enable_metrics);
    assert_eq!(c.metrics_config, "");
    assert_eq!(c.metrics_config_file, "");
    assert!(!c.metrics_ssl_enable);
}

#[test]
fn default_trace_and_observability_ports() {
    let c = defaults();
    assert!(!c.enable_trace);
    assert_eq!(c.trace_config, "");
    assert_eq!(c.observability_agent_grpc_port, 4317);
    assert_eq!(c.observability_prometheus_port, 9392);
    assert_eq!(c.prometheus_pushgateway_port, 9091);
    assert_eq!(c.prometheus_pushgateway_ip, "");
}

#[test]
fn default_metastore_healthcheck_numbers() {
    let c = defaults();
    assert_eq!(c.max_tolerate_metastore_healthcheck_failed_times, 60);
    assert_eq!(c.metastore_healthcheck_interval, 10_000);
    assert_eq!(c.metastore_healthcheck_timeout, 20_000);
}

#[test]
fn default_scheduling_fields() {
    let c = defaults();
    assert_eq!(c.max_priority, 0);
    assert!(!c.enable_preemption);
    assert_eq!(c.aggregated_strategy, "no_aggregate");
    assert_eq!(c.schedule_relaxed, -1);
}

#[test]
fn default_instance_resource_bounds() {
    let c = defaults();
    assert_eq!(c.min_instance_cpu_size, 300);
    assert_eq!(c.max_instance_cpu_size, 16_000);
    assert_eq!(c.min_instance_memory_size, 128);
    assert_eq!(c.max_instance_memory_size, 1_048_576);
}

#[test]
fn default_system_fields() {
    let c = defaults();
    assert_eq!(c.system_timeout, 180_000);
    assert_eq!(c.pull_resource_interval, 500);
    assert_eq!(c.system_auth_mode, "");
    assert_eq!(c.meta_store_excluded_keys, "");
    assert_eq!(c.quota_config_file, "");
}

#[test]
fn parse_explicit_etcd_and_cluster() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--etcd-address",
        "http://127.0.0.1:2379",
        "--etcd-table-prefix",
        "/prefix",
        "--cluster-id",
        "c-1",
    ])
    .unwrap()
    .common;
    assert_eq!(c.etcd_address, "http://127.0.0.1:2379");
    assert_eq!(c.etcd_table_prefix, "/prefix");
    assert_eq!(c.cluster_id, "c-1");
}

#[test]
fn parse_explicit_bools_all_true() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--ssl-enable",
        "--ssl-downgrade-enable",
        "--enable-metrics",
        "--metrics-ssl-enable",
        "--enable-trace",
        "--enable-preemption",
    ])
    .unwrap()
    .common;
    assert!(c.ssl_enable);
    assert!(c.ssl_downgrade_enable);
    assert!(c.enable_metrics);
    assert!(c.metrics_ssl_enable);
    assert!(c.enable_trace);
    assert!(c.enable_preemption);
}

#[test]
fn parse_no_bool_flags_keeps_defaults_false() {
    let c = defaults();
    assert!(!c.ssl_enable);
    assert!(!c.enable_metrics);
}

#[test]
fn parse_empty_string_metrics_config() {
    let c = TestCli::try_parse_from(["yr-test", "--metrics-config", ""])
        .unwrap()
        .common;
    assert_eq!(c.metrics_config, "");
}

#[test]
fn parse_empty_string_trace_config() {
    let c = TestCli::try_parse_from(["yr-test", "--trace-config", ""])
        .unwrap()
        .common;
    assert_eq!(c.trace_config, "");
}

#[test]
fn parse_ports_zero_and_max_u32() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--observability-agent-grpc-port",
        "0",
        "--observability-prometheus-port",
        "4294967295",
        "--prometheus-pushgateway-port",
        "1",
    ])
    .unwrap()
    .common;
    assert_eq!(c.observability_agent_grpc_port, 0);
    assert_eq!(c.observability_prometheus_port, u32::MAX);
    assert_eq!(c.prometheus_pushgateway_port, 1);
}

#[test]
fn parse_max_priority_bounds() {
    let max = TestCli::try_parse_from(["yr-test", "--max-priority", "65535"])
        .unwrap()
        .common;
    assert_eq!(max.max_priority, u16::MAX);
    let zero = TestCli::try_parse_from(["yr-test", "--max-priority", "0"])
        .unwrap()
        .common;
    assert_eq!(zero.max_priority, 0);
}

#[test]
fn parse_schedule_relaxed_negative_and_zero() {
    let neg = TestCli::try_parse_from(["yr-test", "--schedule-relaxed=-100"])
        .unwrap()
        .common;
    assert_eq!(neg.schedule_relaxed, -100);
    let z = TestCli::try_parse_from(["yr-test", "--schedule-relaxed", "0"])
        .unwrap()
        .common;
    assert_eq!(z.schedule_relaxed, 0);
}

#[test]
fn parse_instance_cpu_u64_extremes() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--min-instance-cpu-size",
        "0",
        "--max-instance-cpu-size",
        "18446744073709551615",
    ])
    .unwrap()
    .common;
    assert_eq!(c.min_instance_cpu_size, 0);
    assert_eq!(c.max_instance_cpu_size, u64::MAX);
}

#[test]
fn parse_instance_memory_u64_large() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--min-instance-memory-size",
        "1",
        "--max-instance-memory-size",
        "9223372036854775808",
    ])
    .unwrap()
    .common;
    assert_eq!(c.min_instance_memory_size, 1);
    assert_eq!(c.max_instance_memory_size, 9_223_372_036_854_775_808);
}

#[test]
fn parse_system_timeout_zero() {
    let c = TestCli::try_parse_from(["yr-test", "--system-timeout", "0"])
        .unwrap()
        .common;
    assert_eq!(c.system_timeout, 0);
}

#[test]
fn parse_pull_resource_interval_zero() {
    let c = TestCli::try_parse_from(["yr-test", "--pull-resource-interval", "0"])
        .unwrap()
        .common;
    assert_eq!(c.pull_resource_interval, 0);
}

#[test]
fn parse_aggregated_strategy_custom() {
    let c = TestCli::try_parse_from([
        "yr-test",
        "--aggregated-strategy",
        "strict_pack",
    ])
    .unwrap()
    .common;
    assert_eq!(c.aggregated_strategy, "strict_pack");
}

#[test]
fn load_config_from_json_str_invalid() {
    let r: Result<serde_json::Value, _> = load_config_from_json_str("{not json");
    assert!(r.is_err());
}

#[test]
fn load_config_from_yaml_str_invalid() {
    #[derive(serde::Deserialize)]
    struct T {
        #[allow(dead_code)]
        a: i32,
    }
    let r: Result<T, _> = load_config_from_yaml_str("a: not_int");
    assert!(r.is_err());
}

#[test]
fn load_yaml_file_by_extension() {
    let mut p = PathBuf::from(std::env::temp_dir());
    p.push(format!("yr-common-cfg-{}.yaml", uuid::Uuid::new_v4()));
    let mut f = fs::File::create(&p).unwrap();
    writeln!(f, "hello: world").unwrap();
    drop(f);
    #[derive(serde::Deserialize, PartialEq, Debug)]
    struct Doc {
        hello: String,
    }
    let d: Doc = load_config(&p).unwrap();
    assert_eq!(d.hello, "world");
    let _ = fs::remove_file(&p);
}

#[test]
fn load_json_file_by_extension() {
    let mut p = PathBuf::from(std::env::temp_dir());
    p.push(format!("yr-common-cfg-{}.json", uuid::Uuid::new_v4()));
    fs::write(&p, r#"{"n":42}"#).unwrap();
    #[derive(serde::Deserialize, PartialEq, Debug)]
    struct Doc {
        n: u32,
    }
    let d: Doc = load_config(&p).unwrap();
    assert_eq!(d.n, 42);
    let _ = fs::remove_file(&p);
}

#[test]
fn load_unknown_extension_tries_yaml_then_json() {
    let mut p = PathBuf::from(std::env::temp_dir());
    p.push(format!("yr-common-cfg-{}.cfg", uuid::Uuid::new_v4()));
    fs::write(&p, "k: v").unwrap();
    #[derive(serde::Deserialize, PartialEq, Debug)]
    struct Doc {
        k: String,
    }
    let d: Doc = load_config(&p).unwrap();
    assert_eq!(d.k, "v");
    let _ = fs::remove_file(&p);
}

#[test]
fn load_missing_file_errors() {
    let p = PathBuf::from("/nonexistent/yr-common-no-such-file-999");
    let r: Result<serde_json::Value, _> = load_config(&p);
    assert!(r.is_err());
}
