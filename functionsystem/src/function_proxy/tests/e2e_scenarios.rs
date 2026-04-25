//! In-process E2E-style scenarios for the Rust function system (no etcd / external services).
//!
//! Scenario numbering follows the yr-rust integration checklist (1–2, 4–6). Scenario 3 is omitted here.

mod common;
mod integration;

use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use clap::Parser;
use futures::future::join_all;
use tower::ServiceExt;
use yr_agent::config::Config as AgentConfig;
use yr_agent::http_api::{router as agent_router, HealthState};
use yr_agent::node_manager::NodeManager;
use yr_agent::registration::SchedulerLink;
use yr_agent::rm_client::RuntimeManagerClient;
use yr_iam::config::CliArgs as IamCliArgs;
use yr_iam::routes::build_router as iam_build_router;
use yr_iam::state::AppState;
use yr_master::config::CliArgs as MasterCliArgs;
use yr_master::http::build_router as master_build_router;
use yr_proto::core_service::KillRequest;
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proxy::busproxy::invocation_handler::{InboundAction, InvocationHandler};
use yr_proxy::http_api::{router as proxy_http_router, HttpState as ProxyHttpState};
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};
use yr_proxy::Config as ProxyConfig;
use yr_runtime_manager::http_api::{mark_process_start, router as rm_http_router};
use yr_runtime_manager::Config as RmConfig;

use common::new_bus;
use integration::test_master_state;
use yr_common::types::InstanceState as CommonInstanceState;

// --- Scenario 1: one-click deploy — `install.sh`-style argv must parse (clap only). ---

#[test]
fn scenario1_function_master_parses_install_sh_style_argv() {
    let argv = [
        "function_master",
        "--ip=10.0.0.1:8400",
        "--meta_store_address=127.0.0.1:2379",
        "--log_config=/tmp/fs.log",
        "--etcd_address=127.0.0.1:2379",
        "--node_id=node-1",
        "--sys_func_retry_period=30",
        "--runtime_recover_enable=false",
        "--litebus_thread_num=4",
        "--system_timeout=3600",
        "--enable_metrics=false",
        "--metrics_config=",
        "--metrics_config_file=",
        "--pull_resource_interval=10",
        "--is_schedule_tolerate_abnormal=true",
        "--enable_print_resource_view=false",
        "--schedule_plugins={}",
        "--schedule_relaxed=false",
        "--max_priority=16",
        "--enable_preemption=false",
        "--enable_meta_store=false",
        "--enable_persistence=false",
        "--meta_store_mode=local",
        "--meta_store_excluded_keys=",
        "--election_mode=standalone",
        "--services_path=/services",
        "--lib_path=/opt/fs/lib",
        "--ssl_enable=false",
        "--ssl_base_path=",
        "--etcd_auth_type=",
        "--etcd_root_ca_file=",
        "--etcd_cert_file=",
        "--etcd_key_file=",
        "--etcd_ssl_base_path=",
        "--etcd_table_prefix=/yr",
        "--etcd_target_name_override=",
        "--ssl_root_file=",
        "--ssl_cert_file=",
        "--ssl_key_file=",
        "--function_meta_path=/meta",
        "--enable_trace=false",
        "--trace_config=",
        "--meta_store_max_flush_concurrency=100",
        "--meta_store_max_flush_batch_size=50",
        "--system_tenant_id=0",
    ];
    MasterCliArgs::try_parse_from(argv).expect("function_master argv from install.sh should parse");
}

#[test]
fn scenario1_function_proxy_parses_install_sh_style_argv() {
    let argv = [
        "function_proxy",
        "--address=10.0.0.1:8402",
        "--meta_store_address=127.0.0.1:2379",
        "--etcd_address=127.0.0.1:2379",
        "--node_id=node-1",
        "--log_config=/tmp/fs.log",
        "--services_path=/svc",
        "--lib_path=/opt/fs/lib",
        "--function_meta_path=/meta",
        "--ip=10.0.0.1",
        "--grpc_listen_port=8402",
        "--session_grpc_port=18403",
        "--dposix_uds_path=",
        "--enable_driver=true",
        "--enable_trace=false",
        "--trace_config=",
        "--enable_metrics=false",
        "--metrics_config=",
        "--metrics_config_file=",
        "--litebus_thread_num=4",
        "--update_resource_cycle=1000",
        "--pseudo_data_plane=false",
        "--system_timeout=3600",
        "--global_scheduler_address=10.0.0.1:8400",
        "--runtime_conn_timeout_s=30",
        "--enable_print_resource_view=false",
        "--enable_server_mode=true",
        "--max_priority=16",
        "--enable_preemption=false",
        "--min_instance_memory_size=134217728",
        "--min_instance_cpu_size=0.1",
        "--max_instance_memory_size=68719476736",
        "--max_instance_cpu_size=64.0",
        "--election_mode=standalone",
        "--unregister_while_stop=false",
        "--ssl_downgrade_enable=true",
        "--ssl_enable=false",
        "--ssl_base_path=",
        "--ssl_root_file=",
        "--ssl_cert_file=",
        "--ssl_key_file=",
        "--etcd_auth_type=",
        "--etcd_root_ca_file=",
        "--etcd_cert_file=",
        "--etcd_key_file=",
        "--etcd_ssl_base_path=",
        "--etcd_table_prefix=/yr",
        "--etcd_target_name_override=",
        "--enable_print_perf=false",
        "--enable_meta_store=false",
        "--meta_store_mode=local",
        "--meta_store_excluded_keys=",
        "--http_port=18402",
        "--posix_port=8403",
        "--cache_storage_host=127.0.0.1",
        "--cache_storage_port=31501",
    ];
    ProxyConfig::try_parse_from(argv).expect("function_proxy argv from install.sh should parse");
}

#[test]
fn scenario1_function_agent_parses_install_sh_style_argv() {
    let argv = [
        "function_agent",
        "--enable_merge_process=true",
        "--ip=10.0.0.1",
        "--node_id=node-1",
        "--agent_uid=node-1",
        "--alias=test-alias",
        "--log_config=/tmp/fs.log",
        "--litebus_thread_num=4",
        "--local_scheduler_address=10.0.0.1:8402",
        "--agent_listen_port=22799",
        "--runtime_dir=/runtime/service",
        "--runtime_home_dir=/home/runtime",
        "--runtime_logs_dir=/tmp/rtlogs",
        "--runtime_std_log_dir=",
        "--runtime_ld_library_path=/lib",
        "--runtime_log_level=INFO",
        "--runtime_max_log_size=100",
        "--runtime_max_log_file_num=5",
        "--runtime_config_dir=/cfg",
        "--enable_separated_redirect_runtime_std=false",
        "--user_log_export_mode=file",
        "--npu_collection_mode=off",
        "--gpu_collection_enable=false",
        "--proxy_ip=10.0.0.2",
        "--proxy_grpc_server_port=8402",
        "--setCmdCred=false",
        "--python_dependency_path=/py",
        "--python_log_config_path=/py/log.json",
        "--java_system_property=/java/log.xml",
        "--java_system_library_path=/java/lib",
        "--host_ip=10.0.0.1",
        "--port=18403",
        "--data_system_port=31501",
        "--agent_address=10.0.0.1:22799",
        "--enable_metrics=false",
        "--metrics_config=",
        "--metrics_config_file=",
        "--runtime_initial_port=9000",
        "--port_num=1000",
        "--system_timeout=3600",
        "--metrics_collector_type=prometheus",
        "--proc_metrics_cpu=100",
        "--custom_resources=",
        "--is_protomsg_to_runtime=false",
        "--massif_enable=false",
        "--enable_inherit_env=false",
        "--memory_detection_interval=0",
        "--oom_kill_enable=false",
        "--oom_kill_control_limit=0",
        "--oom_consecutive_detection_count=3",
        "--kill_process_timeout_seconds=0",
        "--runtime_ds_connect_timeout=0",
        "--runtime_direct_connection_enable=false",
        "--ssl_enable=false",
        "--ssl_base_path=",
        "--ssl_root_file=",
        "--ssl_cert_file=",
        "--ssl_key_file=",
        "--etcd_auth_type=",
        "--etcd_root_ca_file=",
        "--etcd_cert_file=",
        "--etcd_key_file=",
        "--etcd_ssl_base_path=",
        "--runtime_default_config=",
        "--proc_metrics_memory=0",
        "--enable_dis_conv_call_stack=false",
        "--data_system_enable=true",
        "--data_system_host=10.0.0.1",
        "--runtime_instance_debug_enable=false",
        "--log_expiration_enable=false",
        "--log_expiration_time_threshold=0",
        "--log_expiration_cleanup_interval=0",
        "--log_expiration_max_file_count=0",
        "--user_log_auto_flush_interval_ms=0",
        "--user_log_buffer_flush_threshold=0",
        "--user_log_rolling_size_limit_mb=0",
        "--user_log_rolling_file_count_limit=0",
        "--npu_collection_enable=false",
        "--local_node_id=node-1",
    ];
    AgentConfig::try_parse_from(argv).expect("function_agent argv from install.sh should parse");
}

#[test]
fn scenario1_runtime_manager_parses_default_argv() {
    RmConfig::try_parse_from(["runtime_manager"]).expect("runtime_manager defaults should parse");
}

#[test]
fn scenario1_iam_server_parses_install_sh_style_argv() {
    let argv = [
        "iam_server",
        "--ip=10.0.0.1",
        "--http_listen_port=8300",
        "--meta_store_address=127.0.0.1:2379",
        "--log_config=/tmp/fs.log",
        "--node_id=node-1",
        "--enable_iam=true",
        "--enable_trace=false",
        "--token_expired_time_span=3600",
        "--iam_credential_type=token",
        "--election_mode=standalone",
        "--ssl_enable=false",
        "--ssl_base_path=",
        "--ssl_root_file=",
        "--ssl_cert_file=",
        "--ssl_key_file=",
        "--auth_provider=casdoor",
        "--keycloak_enabled=false",
        "--keycloak_url=http://127.0.0.1:8080",
        "--keycloak_issuer_url=http://127.0.0.1:8080",
        "--keycloak_realm=yuanrong",
        "--casdoor_enabled=false",
        "--casdoor_endpoint=http://127.0.0.1:8000",
        "--casdoor_public_endpoint=http://127.0.0.1:8000",
        "--casdoor_client_id=",
        "--casdoor_client_secret=",
        "--casdoor_organization=yuanrong",
        "--casdoor_application=app-yuanrong",
        "--casdoor_admin_user=",
        "--casdoor_admin_password=",
        "--casdoor_jwt_public_key=",
    ];
    IamCliArgs::try_parse_from(argv).expect("iam_server argv from install.sh should parse");
}

// --- Scenario 2: `/healthy` with Node-ID + PID headers ---

fn proxy_test_http_state(node_id: &str) -> ProxyHttpState {
    let rv = ResourceView::new(ResourceVector {
        cpu: 4.0,
        memory: 8.0,
        npu: 0.0,
    });
    ProxyHttpState {
        resource_view: rv,
        node_id: node_id.into(),
    }
}

fn in_process_rm_for_health() -> Arc<RuntimeManagerClient> {
    let log = std::env::temp_dir().join(format!("yr_e2e_rm_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(RmConfig::embedded_in_agent(
        "e2e-rm-node".into(),
        "http://127.0.0.1:1".into(),
        "/bin/true".into(),
        40200,
        10,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports =
        Arc::new(yr_runtime_manager::port_manager::SharedPortManager::new(40200, 10).unwrap());
    let st = Arc::new(yr_runtime_manager::state::RuntimeManagerState::new(
        cfg, ports,
    ));
    Arc::new(RuntimeManagerClient::in_process(
        st,
        vec!["/bin/true".to_string()],
    ))
}

#[tokio::test]
async fn scenario2_master_health_ok_without_headers_is_simple_liveness() {
    let app = master_build_router(test_master_state(), None);
    let pid = std::process::id().to_string();
    let ok = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "test-master")
        .header("PID", &pid)
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.clone().oneshot(ok).await.unwrap().status(),
        StatusCode::OK
    );
    let simple = Request::builder()
        .uri("/healthy")
        .body(Body::empty())
        .unwrap();
    assert_eq!(app.oneshot(simple).await.unwrap().status(), StatusCode::OK);
}

#[tokio::test]
async fn scenario2_proxy_health_ok_without_headers_is_simple_liveness() {
    let app = proxy_http_router(proxy_test_http_state("proxy-h"));
    let pid = std::process::id().to_string();
    let ok = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "proxy-h")
        .header("PID", &pid)
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.clone().oneshot(ok).await.unwrap().status(),
        StatusCode::OK
    );
    let simple = Request::builder()
        .uri("/healthy")
        .body(Body::empty())
        .unwrap();
    assert_eq!(app.oneshot(simple).await.unwrap().status(), StatusCode::OK);
}

#[tokio::test]
async fn scenario2_agent_health_ok_without_headers_is_simple_liveness() {
    let rm = in_process_rm_for_health();
    let sched = SchedulerLink::new_arc(
        "http://127.0.0.1:9".into(),
        "agent-h-node".into(),
        "http://127.0.0.1:1".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = agent_router(HealthState {
        rm,
        scheduler: sched,
        node_id: "agent-h-node".into(),
        node,
    });
    let pid = std::process::id().to_string();
    let ok = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "agent-h-node")
        .header("PID", &pid)
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.clone().oneshot(ok).await.unwrap().status(),
        StatusCode::OK
    );
    let simple = Request::builder()
        .uri("/healthy")
        .body(Body::empty())
        .unwrap();
    assert_eq!(app.oneshot(simple).await.unwrap().status(), StatusCode::OK);
}

#[tokio::test]
async fn scenario2_runtime_manager_health_ok_without_headers_is_400() {
    mark_process_start();
    let cfg = Arc::new(RmConfig::try_parse_from(["runtime_manager"]).unwrap());
    let app = rm_http_router(cfg);
    let pid = std::process::id().to_string();
    let ok = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "node-0")
        .header("PID", &pid)
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.clone().oneshot(ok).await.unwrap().status(),
        StatusCode::OK
    );
    let bad = Request::builder()
        .uri("/healthy")
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.oneshot(bad).await.unwrap().status(),
        StatusCode::BAD_REQUEST
    );
}

#[tokio::test]
async fn scenario2_iam_health_ok_without_headers_is_simple_liveness() {
    use std::time::Duration;
    use yr_iam::config::{ElectionMode, IamCredentialType};

    let cfg = yr_iam::config::IamConfig {
        host: "127.0.0.1".into(),
        port: 8300,
        etcd_endpoints: vec!["127.0.0.1:2379".into()],
        cluster_id: "e2e".into(),
        enable_iam: true,
        token_ttl_default: Duration::from_secs(3600),
        election_mode: ElectionMode::Standalone,
        iam_credential_type: IamCredentialType::Token,
        etcd_table_prefix: String::new(),
        iam_signing_secret: "e2e".into(),
        instance_id: "iam-h-node".into(),
    };
    let app = iam_build_router(Arc::new(AppState::new(cfg, None)));
    let pid = std::process::id().to_string();
    let ok = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "iam-h-node")
        .header("PID", &pid)
        .body(Body::empty())
        .unwrap();
    assert_eq!(
        app.clone().oneshot(ok).await.unwrap().status(),
        StatusCode::OK
    );
    let simple = Request::builder()
        .uri("/healthy")
        .body(Body::empty())
        .unwrap();
    assert_eq!(app.oneshot(simple).await.unwrap().status(), StatusCode::OK);
}

// --- Scenario 4: full happy-path state chain on instance metadata. ---

#[test]
fn scenario4_stateful_actor_state_machine_new_through_exited() {
    let mut meta = InstanceMetadata {
        id: "s4".into(),
        function_name: "f".into(),
        tenant: "t".into(),
        node_id: "n".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::New,
        created_at_ms: 0,
        updated_at_ms: 0,
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    assert!(meta.transition(InstanceState::Scheduling).is_ok());
    assert!(meta.transition(InstanceState::Creating).is_ok());
    assert!(meta.transition(InstanceState::Running).is_ok());
    assert!(meta.transition(InstanceState::Exiting).is_ok());
    assert!(meta.transition(InstanceState::Exited).is_ok());
    assert_eq!(meta.state, InstanceState::Exited);
}

// --- Scenario 5: many instances reach Running concurrently. ---

#[tokio::test]
async fn scenario5_batch_concurrent_instances_reach_running() {
    let bus = new_bus("batch-node", 30350);
    let n = 12usize;
    let futs: Vec<_> = (0..n)
        .map(|i| {
            let bus = bus.clone();
            async move {
                let id = format!("batch-inst-{i}");
                let meta = InstanceMetadata {
                    id: id.clone(),
                    function_name: "fn".into(),
                    tenant: "".into(),
                    node_id: "batch-node".into(),
                    runtime_id: "".into(),
                    runtime_port: 0,
                    state: CommonInstanceState::New,
                    created_at_ms: InstanceMetadata::now_ms(),
                    updated_at_ms: InstanceMetadata::now_ms(),
                    group_id: None,
                    trace_id: String::new(),
                    resources: Default::default(),
                    etcd_kv_version: None,
                    etcd_mod_revision: None,
                };
                bus.instance_ctrl_ref().insert_metadata(meta.clone());
                bus.instance_ctrl_ref()
                    .transition_with_version(&id, CommonInstanceState::Scheduling, None)
                    .await
                    .unwrap();
                bus.instance_ctrl_ref()
                    .transition_with_version(&id, CommonInstanceState::Creating, None)
                    .await
                    .unwrap();
                bus.instance_ctrl_ref()
                    .transition_with_version(&id, CommonInstanceState::Running, None)
                    .await
                    .unwrap();
                id
            }
        })
        .collect();
    let ids = join_all(futs).await;
    for id in ids {
        let s = bus.instance_ctrl_ref().get(&id).unwrap().state;
        assert_eq!(s, CommonInstanceState::Running, "instance {id}");
    }
}

// --- Scenario 6: KillReq → Exiting, then normal exit → Exited; multiple signals. ---

async fn kill_then_finalize_exited(
    bus: &std::sync::Arc<yr_proxy::busproxy::BusProxyCoordinator>,
    iid: &str,
    signal: i32,
) {
    let msg = StreamingMessage {
        message_id: format!("kill-{signal}"),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(KillRequest {
            signal,
            instance_id: String::new(),
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) = InvocationHandler::handle_runtime_inbound(iid, msg, bus).await
    else {
        panic!("KillReq should yield Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::KillRsp(_))
    ));
    assert_eq!(
        bus.instance_ctrl_ref().get(iid).unwrap().state,
        CommonInstanceState::Exiting
    );
    bus.instance_ctrl_ref()
        .apply_exit_event(iid, true, "mock runtime exited")
        .await;
    assert_eq!(
        bus.instance_ctrl_ref().get(iid).unwrap().state,
        CommonInstanceState::Exited
    );
}

#[tokio::test]
async fn scenario6_kill_req_transitions_running_through_exited_signal_variants() {
    for (sig, suffix) in [(1, "a"), (2, "b"), (10, "c")] {
        let bus = new_bus("kill-node", 30360);
        let iid = format!("kill-{suffix}");
        bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
            id: iid.clone(),
            function_name: "f".into(),
            tenant: "".into(),
            node_id: "kill-node".into(),
            runtime_id: "rt-1".into(),
            runtime_port: 0,
            state: CommonInstanceState::Running,
            created_at_ms: InstanceMetadata::now_ms(),
            updated_at_ms: InstanceMetadata::now_ms(),
            group_id: None,
            trace_id: String::new(),
            resources: Default::default(),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        });
        kill_then_finalize_exited(&bus, &iid, sig).await;
    }
}
