//! Snapshot-style launch-spec tests for C++ runtime-manager command/env parity.

use std::collections::HashMap;

use clap::Parser;
use yr_proto::internal::StartInstanceRequest;
use yr_runtime_manager::executor::build_runtime_launch_spec;
use yr_runtime_manager::Config;

fn start_req(runtime_type: &str) -> StartInstanceRequest {
    StartInstanceRequest {
        instance_id: "inst-12345678".into(),
        function_name: "hello".into(),
        tenant_id: "tenant-a".into(),
        runtime_type: runtime_type.into(),
        env_vars: HashMap::from([("YR_JOB_ID".into(), "job-trace1234".into())]),
        resources: HashMap::new(),
        code_path: "/srv/func".into(),
        config_json: "{}".into(),
    }
}

#[test]
fn cpp_runtime_launch_args_match_cpp_command_builder_shape() {
    let cfg = Config::try_parse_from([
        "yr-runtime-manager",
        "--host",
        "127.0.0.1",
        "--proxy-ip",
        "10.0.0.2",
        "--runtime-log-level",
        "INFO",
        "--runtime-config-dir",
        "/runtime/config",
    ])
    .unwrap();

    let spec = build_runtime_launch_spec(
        &cfg,
        &start_req("cpp"),
        &["/runtime/cpp/bin/runtime".into()],
        "rt-inst-12345678-1",
        30123,
    )
    .unwrap();

    assert_eq!(spec.executable, "/runtime/cpp/bin/runtime");
    assert_eq!(spec.arg0.as_deref(), Some("cppruntime"));
    assert_eq!(
        spec.args,
        vec![
            "-runtimeId=rt-inst-12345678-1",
            "-logLevel=INFO",
            "-jobId=job-trace1234",
            "-grpcAddress=10.0.0.2:30123",
            "-runtimeConfigPath=/runtime/config/runtime.json",
        ]
    );
    assert!(!spec.args.iter().any(|arg| arg == "inst-12345678"));
}

#[test]
fn launch_env_contains_cpp_runtime_manager_framework_values() {
    let cfg = Config::try_parse_from([
        "yr-runtime-manager",
        "--host",
        "127.0.0.1",
        "--host-ip",
        "10.0.0.1",
        "--proxy-ip",
        "10.0.0.2",
        "--data-system-port",
        "31502",
        "--proxy-grpc-server-port",
        "22775",
        "--driver-server-port",
        "22774",
        "--runtime-dir",
        "/runtime",
        "--runtime-logs-dir",
        "/runtime/logs",
        "--runtime-home-dir",
        "/home/runtime",
        "--runtime-ld-library-path",
        "/extra/lib",
        "--runtime-log-level",
        "WARN",
        "--runtime-max-log-size",
        "80",
        "--runtime-max-log-file-num",
        "9",
        "--runtime-ds-connect-timeout",
        "77",
    ])
    .unwrap();
    let mut req = start_req("cpp");
    req.env_vars
        .insert("UNZIPPED_WORKING_DIR".into(), "/tmp/unzip".into());
    req.env_vars.insert(
        "LD_LIBRARY_PATH".into(),
        "${LD_LIBRARY_PATH}:/custom/lib".into(),
    );

    let spec = build_runtime_launch_spec(
        &cfg,
        &req,
        &["/runtime/cpp/bin/runtime".into()],
        "rt-inst-12345678-1",
        30123,
    )
    .unwrap();

    assert_eq!(
        spec.env.get("POSIX_LISTEN_ADDR").map(String::as_str),
        Some("10.0.0.2:30123")
    );
    assert_eq!(
        spec.env.get("POD_IP").map(String::as_str),
        Some("127.0.0.1")
    );
    assert_eq!(
        spec.env.get("HOST_IP").map(String::as_str),
        Some("10.0.0.1")
    );
    assert_eq!(
        spec.env.get("DATASYSTEM_ADDR").map(String::as_str),
        Some("10.0.0.1:31502")
    );
    assert_eq!(
        spec.env.get("YR_DS_ADDRESS").map(String::as_str),
        Some("10.0.0.1:31502")
    );
    assert_eq!(
        spec.env.get("YR_SERVER_ADDRESS").map(String::as_str),
        Some("10.0.0.2:22775")
    );
    assert_eq!(
        spec.env.get("PROXY_GRPC_SERVER_PORT").map(String::as_str),
        Some("22775")
    );
    assert_eq!(
        spec.env.get("DRIVER_SERVER_PORT").map(String::as_str),
        Some("22774")
    );
    assert_eq!(
        spec.env.get("HOME").map(String::as_str),
        Some("/home/runtime")
    );
    assert_eq!(
        spec.env.get("FUNCTION_LIB_PATH").map(String::as_str),
        Some("/srv/func")
    );
    assert_eq!(
        spec.env.get("YR_FUNCTION_LIB_PATH").map(String::as_str),
        Some("/srv/func")
    );
    assert_eq!(
        spec.env.get("YR_LOG_LEVEL").map(String::as_str),
        Some("WARN")
    );
    assert_eq!(
        spec.env.get("GLOG_log_dir").map(String::as_str),
        Some("/runtime/logs")
    );
    assert_eq!(
        spec.env.get("YR_MAX_LOG_SIZE_MB").map(String::as_str),
        Some("80")
    );
    assert_eq!(
        spec.env.get("YR_MAX_LOG_FILE_NUM").map(String::as_str),
        Some("9")
    );
    assert_eq!(
        spec.env.get("DS_CONNECT_TIMEOUT_SEC").map(String::as_str),
        Some("77")
    );
    assert!(!spec.env.contains_key("UNZIPPED_WORKING_DIR"));

    let ld = spec.env.get("LD_LIBRARY_PATH").expect("ld path");
    assert!(
        ld.starts_with("/srv/func:/srv/func/lib:/runtime/cpp/lib:/extra/lib"),
        "{ld}"
    );
    assert!(ld.ends_with(":/custom/lib"), "{ld}");
}

#[test]
fn set_cmd_cred_adds_runtime_credential_to_launch_spec() {
    let cfg = Config::try_parse_from([
        "yr-runtime-manager",
        "--setCmdCred=true",
        "--runtime-uid",
        "1003",
        "--runtime-gid",
        "1004",
    ])
    .unwrap();

    let spec = build_runtime_launch_spec(
        &cfg,
        &start_req("cpp"),
        &["/runtime/cpp/bin/runtime".into()],
        "rt-inst-12345678-1",
        30123,
    )
    .unwrap();

    let cred = spec
        .credential
        .expect("setCmdCred should request uid/gid hook");
    assert_eq!(cred.uid, 1003);
    assert_eq!(cred.gid, 1004);
}

#[test]
fn python_runtime_launch_keeps_python_server_entry_and_cpp_envs() {
    let cfg = Config::try_parse_from([
        "yr-runtime-manager",
        "--host",
        "127.0.0.1",
        "--proxy-ip",
        "10.0.0.2",
        "--runtime-log-level",
        "DEBUG",
    ])
    .unwrap();
    let paths = vec!["/runtime/service/python/yr/main/yr_runtime_main.py".into()];

    let spec = build_runtime_launch_spec(&cfg, &start_req("python3.11"), &paths, "rt-py-1", 30124)
        .unwrap();

    assert_eq!(spec.executable, "python3.11");
    assert_eq!(spec.arg0, None);
    assert_eq!(spec.args[0], "-u");
    assert_eq!(
        spec.args[1],
        "/runtime/service/python/yr/main/yr_runtime_main.py"
    );
    assert!(spec
        .args
        .windows(2)
        .any(|w| w == ["--rt_server_address", "10.0.0.2:30124"]));
    assert!(spec
        .args
        .windows(2)
        .any(|w| w == ["--deploy_dir", "/srv/func"]));
    assert!(spec
        .args
        .windows(2)
        .any(|w| w == ["--runtime_id", "rt-py-1"]));
    assert!(spec
        .args
        .windows(2)
        .any(|w| w == ["--job_id", "job-trace1234"]));
    assert_eq!(
        spec.env.get("POSIX_LISTEN_ADDR").map(String::as_str),
        Some("10.0.0.2:30124")
    );
    assert_eq!(
        spec.env.get("PYTHONUNBUFFERED").map(String::as_str),
        Some("1")
    );
}
