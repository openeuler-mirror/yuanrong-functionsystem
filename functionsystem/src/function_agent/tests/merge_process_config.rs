//! Merge-process (embedded runtime manager) wiring from agent CLI flags.

use std::path::PathBuf;

use clap::Parser;
use yr_agent::config::Config;

#[test]
fn embedded_rm_config_reflects_merge_flags() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--host",
        "0.0.0.0",
        "--node-id",
        "worker-merge-1",
        "--enable-merge-process",
        "--merge-runtime-paths",
        "/bin/sleep,/bin/cat",
        "--merge-runtime-initial-port",
        "13000",
        "--merge-port-count",
        "64",
        "--merge-runtime-log-path",
        "/tmp/merge-logs-test",
        "--merge-runtime-bind-mounts",
        "/data:/work:ro",
    ])
    .unwrap();

    let rm = yr_runtime_manager::Config::embedded_in_agent(
        c.node_id.clone(),
        c.agent_grpc_endpoint(),
        c.merge_runtime_paths.clone(),
        c.merge_runtime_initial_port,
        c.merge_port_count,
        PathBuf::from(&c.merge_runtime_log_path),
        c.merge_runtime_bind_mounts.clone(),
    );

    assert_eq!(rm.node_id, "worker-merge-1");
    assert_eq!(rm.agent_address, c.agent_grpc_endpoint());
    assert_eq!(rm.runtime_initial_port, 13000);
    assert_eq!(rm.port_count, 64);
    assert_eq!(rm.runtime_paths, "/bin/sleep,/bin/cat");
    assert_eq!(rm.log_path, PathBuf::from("/tmp/merge-logs-test"));
    assert_eq!(rm.extra_bind_mounts, "/data:/work:ro");
}

#[test]
fn embedded_rm_uses_loopback_and_zero_grpc_port() {
    let c = Config::try_parse_from(["yr-agent", "--node-id", "n"]).unwrap();
    let rm = yr_runtime_manager::Config::embedded_in_agent(
        c.node_id.clone(),
        "http://127.0.0.1:22799".into(),
        "/bin/true".into(),
        14000,
        5,
        PathBuf::from("/tmp/x"),
        "".into(),
    );
    assert_eq!(rm.host, "127.0.0.1");
    assert_eq!(rm.port, 0);
    assert!(rm.http_host.is_none());
    assert!(rm.http_port.is_none());
}

#[test]
fn agent_grpc_endpoint_used_as_rm_agent_address() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--host",
        "10.1.1.1",
        "--agent-listen-port",
        "33000",
    ])
    .unwrap();
    let ep = c.agent_grpc_endpoint();
    let rm = yr_runtime_manager::Config::embedded_in_agent(
        "nid".into(),
        ep.clone(),
        "/bin/sleep".into(),
        9000,
        1,
        PathBuf::from("/tmp/y"),
        "".into(),
    );
    assert_eq!(rm.agent_address, ep);
    assert_eq!(ep, "http://10.1.1.1:33000");
}

#[test]
fn agent_cpp_runtime_flags_feed_embedded_runtime_manager_config() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--host",
        "172.17.0.2",
        "--node-id",
        "node-cpp",
        "--runtime_dir",
        "/runtime/service",
        "--runtime_home_dir",
        "/home/runtime",
        "--runtime_config_dir",
        "/runtime/config",
        "--runtime_logs_dir",
        "/runtime/logs",
        "--snuser_lib_dir",
        "/runtime/lib",
        "--runtime_ld_library_path",
        "/operator/lib",
        "--runtime_log_level",
        "INFO",
        "--runtime_max_log_size",
        "88",
        "--runtime_max_log_file_num",
        "11",
        "--runtime_ds_connect_timeout",
        "66",
        "--proxy_ip",
        "10.0.0.2",
        "--host_ip",
        "10.0.0.1",
        "--proxy_grpc_server_port",
        "22775",
        "--data_system_port",
        "31502",
        "--driver_server_port",
        "22774",
        "--python_dependency_path",
        "/pydeps",
        "--python_log_config_path",
        "/py/log.json",
        "--java_system_property",
        "/java/log.xml",
        "--java_system_library_path",
        "/java/lib",
        "--enable_inherit_env=true",
        "--setCmdCred=true",
    ])
    .unwrap();

    let rm = c.embedded_runtime_manager_config();

    assert_eq!(rm.host, "172.17.0.2");
    assert_eq!(rm.node_id, "node-cpp");
    assert_eq!(rm.runtime_dir, "/runtime/service");
    assert_eq!(rm.runtime_home_dir, "/home/runtime");
    assert_eq!(rm.runtime_config_dir, "/runtime/config");
    assert_eq!(rm.runtime_logs_dir, "/runtime/logs");
    assert_eq!(rm.snuser_lib_dir, "/runtime/lib");
    assert!(rm.runtime_ld_library_path.contains("/operator/lib"));
    assert!(rm.runtime_ld_library_path.contains("/runtime/service/cpp/lib"));
    assert_eq!(rm.runtime_log_level, "INFO");
    assert_eq!(rm.runtime_max_log_size, 88);
    assert_eq!(rm.runtime_max_log_file_num, 11);
    assert_eq!(rm.runtime_ds_connect_timeout, 66);
    assert_eq!(rm.proxy_ip, "10.0.0.2");
    assert_eq!(rm.host_ip, "10.0.0.1");
    assert_eq!(rm.proxy_grpc_server_port, "22775");
    assert_eq!(rm.data_system_port, "31502");
    assert_eq!(rm.driver_server_port, "22774");
    assert_eq!(rm.python_dependency_path, "/pydeps");
    assert_eq!(rm.python_log_config_path, "/py/log.json");
    assert_eq!(rm.java_system_property, "/java/log.xml");
    assert_eq!(rm.java_system_library_path, "/java/lib");
    assert!(rm.enable_inherit_env);
    assert!(rm.set_cmd_cred);
}
