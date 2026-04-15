//! Focused default-value checks for `Config` (including C++ parity fields).

use clap::Parser;
use yr_runtime_manager::Config;

#[test]
fn defaults_network_agent_and_ports() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 8404);
    assert!(c.http_host.is_none());
    assert!(c.http_port.is_none());
    assert_eq!(c.http_listen_addr(), "0.0.0.0:8405");
    assert_eq!(c.node_id, "node-0");
    assert_eq!(c.agent_address, "http://127.0.0.1:8403");
    assert_eq!(c.runtime_initial_port, 9000);
    assert_eq!(c.port_count, 1000);
}

#[test]
fn defaults_cgroup_isolation_and_logs() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert_eq!(c.cgroup_parent.to_string_lossy(), "");
    assert!(c.cgroup_enable_cpu);
    assert!(c.cgroup_enable_memory);
    assert!(!c.isolate_namespaces);
    assert_eq!(c.extra_bind_mounts, "");
    assert_eq!(c.log_path.to_string_lossy(), "/tmp/yr-runtime-logs");
    assert_eq!(c.log_rotate_max_bytes, 64 * 1024 * 1024);
    assert_eq!(c.log_rotate_keep, 3);
}

#[test]
fn defaults_health_probes_and_metrics_interval() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert_eq!(c.metrics_interval_ms, 5000);
    assert_eq!(c.instance_health_interval_ms, 5000);
    assert_eq!(c.manager_health_http_url, "");
    assert_eq!(c.manager_health_tcp, "");
    assert_eq!(c.manager_startup_probe_secs, 30);
}

#[test]
fn defaults_cpp_network_and_runtime_identity_fields() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
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
}

#[test]
fn defaults_disk_oom_env_and_kill_timeout() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert!(!c.oom_kill_enable);
    assert_eq!(c.oom_kill_control_limit, 0);
    assert_eq!(c.oom_consecutive_detection_count, 3);
    assert!(!c.disk_usage_monitor_notify_failure_enable);
    assert_eq!(c.disk_usage_monitor_path, "/tmp");
    assert_eq!(c.disk_usage_limit, -1);
    assert_eq!(c.custom_resources, "");
    assert!(!c.enable_inherit_env);
    assert!(!c.set_cmd_cred);
    assert_eq!(c.kill_process_timeout_seconds, 0);
}
