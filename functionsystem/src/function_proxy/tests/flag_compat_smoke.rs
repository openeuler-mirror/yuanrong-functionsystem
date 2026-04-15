//! CLI flag compatibility smoke tests for function proxy / local scheduler (`Config`).

use clap::{CommandFactory, Parser};
use yr_proxy::Config;

fn long_help() -> String {
    format!("{}", Config::command().render_long_help())
}

#[test]
fn default_values_match_expectations() {
    let c = Config::try_parse_from(["yr-proxy"]).expect("parse defaults");
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 8402);
    assert_eq!(c.posix_port, 8403);
    assert_eq!(c.http_port, 18402);
    assert_eq!(c.session_grpc_port, 18403);
    assert_eq!(c.global_scheduler_address, "");
    assert_eq!(c.domain_scheduler_address, "");
    assert_eq!(c.etcd_endpoints, "");
    assert_eq!(c.etcd_table_prefix, "");
    assert_eq!(c.node_id, "");
    assert!(!c.enable_preemption);
    assert_eq!(c.min_instance_cpu, 0.1);
    assert_eq!(c.max_instance_cpu, 64.0);
    assert_eq!(c.min_instance_memory, 134_217_728.0);
    assert_eq!(c.max_instance_memory, 68_719_476_736.0);
    assert_eq!(c.schedule_plugins, "{}");
    assert_eq!(c.data_system_host, "127.0.0.1");
    assert_eq!(c.data_system_port, 0);
    assert_eq!(c.runtime_manager_address, "");
    assert_eq!(c.create_rate_limit_per_sec, 0);
    assert_eq!(c.proxy_aid, "");
    assert_eq!(c.proxy_access_key, "");
    assert_eq!(c.busproxy_tenant_segment, "0");
    assert_eq!(c.busproxy_lease_ttl_sec, 30);
    assert_eq!(c.posix_uds_path, "");
    assert_eq!(c.exec_session_idle_sec, 120);

    assert_eq!(c.election_mode, "standalone");
    assert_eq!(c.state_storage_type, "disable");
    assert!(c.enable_server_mode);
    assert!(!c.enable_driver);
    assert!(!c.runtime_recover_enable);
    assert!(c.runtime_heartbeat_enable);
    assert_eq!(c.runtime_max_heartbeat_timeout_times, 5);
    assert_eq!(c.runtime_heartbeat_timeout_ms, 5000);
    assert_eq!(c.runtime_init_call_timeout_seconds, 300);
    assert_eq!(c.runtime_shutdown_timeout_seconds, 30);
    assert_eq!(c.runtime_conn_timeout_s, 30);
    assert_eq!(c.max_grpc_size, 11);
    assert!(!c.enable_iam);
    assert_eq!(c.iam_base_path, "");
    assert_eq!(c.iam_policy_file, "");
    assert_eq!(c.iam_meta_store_address, "");
    assert_eq!(c.iam_credential_type, "token");
    assert!(!c.enable_tenant_affinity);
    assert!(!c.enable_merge_process);
    assert_eq!(c.merge_runtime_paths, "/bin/sleep");
    assert_eq!(c.merge_runtime_initial_port, 9000);
    assert_eq!(c.merge_port_count, 1000);
    assert_eq!(c.merge_runtime_log_path, "/tmp/yr-proxy-runtime-logs");
    assert_eq!(c.merge_runtime_bind_mounts, "");
    assert!(!c.forward_compatibility);
    assert_eq!(c.decrypt_algorithm, "NO_CRYPTO");
    assert!(!c.enable_print_resource_view);
    assert!(!c.enable_print_perf);
    assert!(!c.enable_meta_store);
    assert_eq!(c.meta_store_address, "");
    assert_eq!(c.meta_store_mode, "local");
    assert_eq!(c.service_register_times, 1000);
    assert_eq!(c.service_register_cycle, 10000);
    assert_eq!(c.service_ttl, 300_000);
    assert!(!c.unregister_while_stop);
}

#[test]
fn key_flags_parse_correctly() {
    let c = Config::try_parse_from([
        "yr-proxy",
        "--host",
        "10.0.0.2",
        "--grpc-listen-port",
        "9402",
        "--posix-port",
        "9403",
        "--http-port",
        "19402",
        "--session-grpc-port",
        "19403",
        "--global-scheduler-address",
        "http://127.0.0.1:8400",
        "--domain-scheduler-address",
        "127.0.0.1:8401",
        "--etcd-endpoints",
        "127.0.0.1:2379,127.0.0.1:2380",
        "--etcd-table-prefix",
        "/yr",
        "--node-id",
        "proxy-1",
        "--enable-preemption",
        "--runtime-manager-address",
        "http://127.0.0.1:8404",
        "--data-system-host",
        "10.0.0.5",
        "--data-system-port",
        "31501",
        "--exec-session-idle-sec",
        "60",
        "--election-mode",
        "cluster",
        "--state-storage-type",
        "redis",
        "--enable-driver",
        "--enable-server-mode",
        "false",
        "--runtime-recover-enable",
        "--runtime-heartbeat-enable",
        "false",
        "--runtime-max-heartbeat-timeout-times",
        "7",
        "--runtime-heartbeat-timeout-ms",
        "3000",
        "--runtime-init-call-timeout-seconds",
        "120",
        "--runtime-shutdown-timeout-seconds",
        "45",
        "--runtime-conn-timeout-s",
        "60",
        "--max-grpc-size",
        "32",
        "--enable-iam",
        "--iam-base-path",
        "/iam",
        "--iam-policy-file",
        "/policy.json",
        "--iam-meta-store-address",
        "127.0.0.1:2379",
        "--iam-credential-type",
        "cert",
        "--enable-tenant-affinity",
        "--enable-merge-process",
        "--forward-compatibility",
        "--decrypt-algorithm",
        "AES",
        "--enable-print-resource-view",
        "--enable-print-perf",
        "--enable-meta-store",
        "--meta-store-address",
        "etcd://meta",
        "--meta-store-mode",
        "remote",
        "--service-register-times",
        "500",
        "--service-register-cycle",
        "5000",
        "--service-ttl",
        "60000",
        "--unregister-while-stop",
    ])
    .expect("parse explicit flags");

    assert_eq!(c.host, "10.0.0.2");
    assert_eq!(c.port, 9402);
    assert_eq!(c.posix_port, 9403);
    assert_eq!(c.http_port, 19402);
    assert_eq!(c.session_grpc_port, 19403);
    assert_eq!(c.global_scheduler_address, "http://127.0.0.1:8400");
    assert_eq!(c.domain_scheduler_address, "127.0.0.1:8401");
    assert_eq!(c.etcd_endpoints, "127.0.0.1:2379,127.0.0.1:2380");
    assert_eq!(c.etcd_table_prefix, "/yr");
    assert_eq!(c.node_id, "proxy-1");
    assert!(c.enable_preemption);
    assert_eq!(c.runtime_manager_address, "http://127.0.0.1:8404");
    assert_eq!(c.data_system_host, "10.0.0.5");
    assert_eq!(c.data_system_port, 31501);
    assert_eq!(c.exec_session_idle_sec, 60);

    assert_eq!(c.election_mode, "cluster");
    assert_eq!(c.state_storage_type, "redis");
    assert!(!c.enable_server_mode);
    assert!(c.enable_driver);
    assert!(c.runtime_recover_enable);
    assert!(!c.runtime_heartbeat_enable);
    assert_eq!(c.runtime_max_heartbeat_timeout_times, 7);
    assert_eq!(c.runtime_heartbeat_timeout_ms, 3000);
    assert_eq!(c.runtime_init_call_timeout_seconds, 120);
    assert_eq!(c.runtime_shutdown_timeout_seconds, 45);
    assert_eq!(c.runtime_conn_timeout_s, 60);
    assert_eq!(c.max_grpc_size, 32);
    assert!(c.enable_iam);
    assert_eq!(c.iam_base_path, "/iam");
    assert_eq!(c.iam_policy_file, "/policy.json");
    assert_eq!(c.iam_meta_store_address, "127.0.0.1:2379");
    assert_eq!(c.iam_credential_type, "cert");
    assert!(c.enable_tenant_affinity);
    assert!(c.enable_merge_process);
    assert!(c.forward_compatibility);
    assert_eq!(c.decrypt_algorithm, "AES");
    assert!(c.enable_print_resource_view);
    assert!(c.enable_print_perf);
    assert!(c.enable_meta_store);
    assert_eq!(c.meta_store_address, "etcd://meta");
    assert_eq!(c.meta_store_mode, "remote");
    assert_eq!(c.service_register_times, 500);
    assert_eq!(c.service_register_cycle, 5000);
    assert_eq!(c.service_ttl, 60000);
    assert!(c.unregister_while_stop);

    let eps = c.etcd_endpoints_vec();
    assert_eq!(
        eps,
        vec!["127.0.0.1:2379".to_string(), "127.0.0.1:2380".to_string()]
    );
}

#[test]
fn port_defaults_match_cpp_conventions() {
    let c = Config::try_parse_from(["yr-proxy"]).expect("parse defaults");
    assert_eq!(c.port, 8402, "function_proxy primary gRPC");
    assert_eq!(c.posix_port, 8403, "POSIX / driver-facing port");
}

#[test]
fn help_documents_key_operational_flags() {
    let help = long_help();
    for needle in [
        "--host",
        "--grpc-listen-port",
        "--posix-port",
        "--http-port",
        "--global-scheduler-address",
        "--domain-scheduler-address",
        "--etcd-endpoints",
        "--runtime-manager-address",
        "--election-mode",
        "--state-storage-type",
        "--enable-server-mode",
        "--runtime-heartbeat-enable",
        "--max-grpc-size",
        "--enable-iam",
        "--meta-store-mode",
    ] {
        assert!(help.contains(needle), "help should mention `{needle}`");
    }
}
