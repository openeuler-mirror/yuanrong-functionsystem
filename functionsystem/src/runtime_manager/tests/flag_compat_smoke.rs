//! CLI flag compatibility smoke tests for runtime manager (`Config`).

use clap::{CommandFactory, Parser};
use yr_runtime_manager::Config;

fn long_help() -> String {
    format!("{}", Config::command().render_long_help())
}

#[test]
fn default_values_match_expectations() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).expect("parse defaults");
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 8404);
    assert!(c.http_host.is_none());
    assert!(c.http_port.is_none());
    assert_eq!(c.node_id, "node-0");
    assert_eq!(c.agent_address, "http://127.0.0.1:8403");
    assert_eq!(c.runtime_initial_port, 9000);
    assert_eq!(c.port_count, 1000);
    assert_eq!(c.runtime_paths, "/bin/sleep");
    assert_eq!(c.log_path.to_string_lossy(), "/tmp/yr-runtime-logs");
    assert_eq!(c.metrics_interval_ms, 5000);
    assert_eq!(c.cgroup_parent.to_string_lossy(), "");
    assert!(c.cgroup_enable_cpu);
    assert!(c.cgroup_enable_memory);
    assert!(!c.isolate_namespaces);
    assert_eq!(c.extra_bind_mounts, "");
    assert_eq!(c.log_rotate_max_bytes, 64 * 1024 * 1024);
    assert_eq!(c.log_rotate_keep, 3);
    assert_eq!(c.instance_health_interval_ms, 5000);
    assert_eq!(c.manager_health_http_url, "");
    assert_eq!(c.manager_health_tcp, "");
    assert_eq!(c.manager_startup_probe_secs, 30);
    assert_eq!(c.http_listen_addr(), "0.0.0.0:8405");

    // C++ parity flags — defaults
    assert_eq!(c.host_ip, "");
    assert_eq!(c.data_system_port, "31501");
    assert_eq!(c.driver_server_port, "22773");
    assert_eq!(c.proxy_grpc_server_port, "22773");
    assert_eq!(c.proxy_ip, "");
    assert_eq!(c.runtime_uid, 0);
    assert_eq!(c.runtime_gid, 0);
    assert_eq!(c.runtime_dir, "/home/snuser");
    assert_eq!(c.snuser_lib_dir, "/home/snuser/lib");
    assert_eq!(c.runtime_ld_library_path, "");
    assert_eq!(c.runtime_log_level, "DEBUG");
    assert_eq!(c.runtime_max_log_size, 40);
    assert_eq!(c.runtime_max_log_file_num, 20);
    assert_eq!(c.python_dependency_path, "/");
    assert!(!c.oom_kill_enable);
    assert_eq!(c.oom_kill_control_limit, 0);
    assert_eq!(c.oom_consecutive_detection_count, 3);
    assert!(!c.disk_usage_monitor_notify_failure_enable);
    assert_eq!(c.disk_usage_monitor_path, "/tmp");
    assert_eq!(c.disk_usage_limit, -1);
    assert_eq!(c.disk_resources, "");
    assert_eq!(c.custom_resources, "");
    assert!(!c.numa_collection_enable);
    assert!(!c.enable_inherit_env);
    assert!(!c.set_cmd_cred);
    assert_eq!(c.kill_process_timeout_seconds, 0);
}

#[test]
fn key_flags_parse_correctly() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--host",
        "10.0.0.4",
        "--port",
        "9404",
        "--http-host",
        "127.0.0.1",
        "--http-port",
        "9406",
        "--node-id",
        "node-9",
        "--agent-address",
        "http://10.0.0.3:22799",
        "--runtime-initial-port",
        "9100",
        "--port-count",
        "250",
        "--runtime-paths",
        "/opt/r1,/opt/r2",
        "--log-path",
        "/var/log/yr-rm",
        "--metrics-interval-ms",
        "2500",
        "--cgroup-parent",
        "/sys/fs/cgroup/yr_test",
        "--isolate-namespaces",
        "--instance-health-interval-ms",
        "8000",
        "--manager-startup-probe-secs",
        "45",
    ])
    .expect("parse explicit flags");

    assert_eq!(c.host, "10.0.0.4");
    assert_eq!(c.port, 9404);
    assert_eq!(c.http_host.as_deref(), Some("127.0.0.1"));
    assert_eq!(c.http_port, Some(9406));
    assert_eq!(c.node_id, "node-9");
    assert_eq!(c.agent_address, "http://10.0.0.3:22799");
    assert_eq!(c.runtime_initial_port, 9100);
    assert_eq!(c.port_count, 250);
    assert_eq!(c.runtime_paths, "/opt/r1,/opt/r2");
    assert_eq!(c.log_path.to_string_lossy(), "/var/log/yr-rm");
    assert_eq!(c.metrics_interval_ms, 2500);
    assert_eq!(c.cgroup_parent.to_string_lossy(), "/sys/fs/cgroup/yr_test");
    assert!(c.cgroup_enable_cpu);
    assert!(c.isolate_namespaces);
    assert_eq!(c.instance_health_interval_ms, 8000);
    assert_eq!(c.manager_startup_probe_secs, 45);
    assert_eq!(c.http_listen_addr(), "127.0.0.1:9406");
}

#[test]
fn cpp_parity_flags_parse_explicitly() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--host-ip",
        "10.1.2.3",
        "--data-system-port",
        "31502",
        "--driver-server-port",
        "22774",
        "--proxy-grpc-server-port",
        "22775",
        "--proxy-ip",
        "10.1.2.4",
        "--runtime-uid",
        "1000",
        "--runtime-gid",
        "1001",
        "--runtime-dir",
        "/var/runtimes",
        "--snuser-lib-dir",
        "/var/runtimes/lib",
        "--runtime-ld-library-path",
        "/opt/lib",
        "--runtime-log-level",
        "INFO",
        "--runtime-max-log-size",
        "80",
        "--runtime-max-log-file-num",
        "10",
        "--python-dependency-path",
        "/deps",
        "--oom-kill-enable",
        "--oom-kill-control-limit",
        "512",
        "--oom-consecutive-detection-count",
        "5",
        "--disk-usage-monitor-notify-failure-enable",
        "--disk-usage-monitor-path",
        "/data",
        "--disk-usage-limit",
        "1024",
        "--disk_resources",
        r#"[{"name":"fast","size":"40G","mountPoints":"/mnt/fast/"}]"#,
        "--custom-resources",
        "gpu:1",
        "--numa_collection_enable=true",
        "--enable-inherit-env",
        "--setCmdCred",
        "--kill-process-timeout-seconds",
        "120",
    ])
    .expect("parse cpp parity flags");

    assert_eq!(c.host_ip, "10.1.2.3");
    assert_eq!(c.data_system_port, "31502");
    assert_eq!(c.driver_server_port, "22774");
    assert_eq!(c.proxy_grpc_server_port, "22775");
    assert_eq!(c.proxy_ip, "10.1.2.4");
    assert_eq!(c.runtime_uid, 1000);
    assert_eq!(c.runtime_gid, 1001);
    assert_eq!(c.runtime_dir, "/var/runtimes");
    assert_eq!(c.snuser_lib_dir, "/var/runtimes/lib");
    assert_eq!(c.runtime_ld_library_path, "/opt/lib");
    assert_eq!(c.runtime_log_level, "INFO");
    assert_eq!(c.runtime_max_log_size, 80);
    assert_eq!(c.runtime_max_log_file_num, 10);
    assert_eq!(c.python_dependency_path, "/deps");
    assert!(c.oom_kill_enable);
    assert_eq!(c.oom_kill_control_limit, 512);
    assert_eq!(c.oom_consecutive_detection_count, 5);
    assert!(c.disk_usage_monitor_notify_failure_enable);
    assert_eq!(c.disk_usage_monitor_path, "/data");
    assert_eq!(c.disk_usage_limit, 1024);
    assert_eq!(
        c.disk_resources,
        r#"[{"name":"fast","size":"40G","mountPoints":"/mnt/fast/"}]"#
    );
    assert_eq!(c.custom_resources, "gpu:1");
    assert!(c.numa_collection_enable);
    assert!(c.enable_inherit_env);
    assert!(c.set_cmd_cred);
    assert_eq!(c.kill_process_timeout_seconds, 120);
}

#[test]
fn port_defaults_match_cpp_conventions() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).expect("parse defaults");
    assert_eq!(
        c.port, 8404,
        "runtime manager gRPC (after 8402 proxy / 8403 posix)"
    );
    assert_eq!(
        c.http_listen_addr(),
        "0.0.0.0:8405",
        "HTTP defaults to gRPC port + 1 when --http-port omitted"
    );
}

#[test]
fn help_documents_key_operational_flags() {
    let help = long_help();
    for needle in [
        "--host",
        "--port",
        "--http-port",
        "--http-host",
        "--agent-address",
        "--runtime-paths",
        "--runtime-initial-port",
        "--port-count",
        "--host-ip",
        "--data-system-port",
        "--runtime-dir",
        "--disk_resources",
        "--setCmdCred",
        "--kill-process-timeout-seconds",
    ] {
        assert!(help.contains(needle), "help should mention `{needle}`");
    }
}
