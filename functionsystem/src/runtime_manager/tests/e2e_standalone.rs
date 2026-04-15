//! E2E: standalone runtime manager gRPC — listen, start/stop, port pool.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use clap::Parser;
use tokio::sync::broadcast;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::Server;
use yr_proto::internal::runtime_manager_service_client::RuntimeManagerServiceClient;
use yr_proto::internal::runtime_manager_service_server::RuntimeManagerServiceServer;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::service::RuntimeManagerGrpc;
use yr_runtime_manager::state::RuntimeManagerState;
use yr_runtime_manager::Config;

#[tokio::test]
async fn standalone_grpc_start_stop_and_port_allocation() {
    let log = std::env::temp_dir().join(format!("yr_rm_e2e_standalone_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let probe = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = probe.local_addr().unwrap().port();
    drop(probe);

    let cfg = Arc::new(
        Config::try_parse_from([
            "yr-runtime-manager",
            "--host",
            "127.0.0.1",
            "--port",
            &port.to_string(),
            "--runtime-initial-port",
            "43100",
            "--port-count",
            "20",
            "--runtime-paths",
            "/bin/true",
            "--log-path",
            log.to_str().unwrap(),
            "--proxy-ip",
            "127.0.0.1",
        ])
        .unwrap(),
    );
    cfg.ensure_log_dir().unwrap();
    assert_ne!(cfg.port, 0);

    let ports = Arc::new(
        SharedPortManager::new(cfg.runtime_initial_port, cfg.port_count).expect("pool"),
    );
    let state = Arc::new(RuntimeManagerState::new(cfg.clone(), ports.clone()));
    let grpc = RuntimeManagerGrpc::new(cfg.clone(), state.clone());
    let svc = RuntimeManagerServiceServer::new(grpc);

    let listener = tokio::net::TcpListener::bind(cfg.grpc_listen_addr())
        .await
        .expect("bind rm grpc");
    let incoming = TcpListenerStream::new(listener);
    let (shutdown_tx, mut shutdown_rx) = broadcast::channel::<()>(1);
    let shutdown_srv = shutdown_tx.clone();
    let server_task = tokio::spawn(async move {
        Server::builder()
            .add_service(svc)
            .serve_with_incoming_shutdown(incoming, async move {
                let _ = shutdown_srv.subscribe().recv().await;
            })
            .await
            .expect("serve");
    });

    tokio::time::sleep(Duration::from_millis(80)).await;
    let uri = format!("http://{}", cfg.grpc_listen_addr());
    let mut client = RuntimeManagerServiceClient::connect(uri)
        .await
        .expect("grpc connect");

    let start = StartInstanceRequest {
        instance_id: "e2e-standalone-1".into(),
        function_name: "f".into(),
        tenant_id: "t".into(),
        runtime_type: "0".into(),
        env_vars: HashMap::new(),
        resources: HashMap::new(),
        code_path: ".".into(),
        config_json: "{}".into(),
    };
    let s1 = client
        .start_instance(tonic::Request::new(start.clone()))
        .await
        .expect("start")
        .into_inner();
    assert!(s1.success);
    let p1 = s1.runtime_port;
    assert!(p1 >= 43100 && (p1 as u32) < 43100 + 20);

    let stop = StopInstanceRequest {
        instance_id: "e2e-standalone-1".into(),
        runtime_id: s1.runtime_id.clone(),
        force: true,
    };
    let st = client
        .stop_instance(tonic::Request::new(stop))
        .await
        .expect("stop")
        .into_inner();
    assert!(st.success);

    let s2 = client
        .start_instance(tonic::Request::new(StartInstanceRequest {
            instance_id: "e2e-standalone-2".into(),
            ..start
        }))
        .await
        .expect("start2")
        .into_inner();
    assert!(s2.success);
    assert_ne!(s2.runtime_id, s1.runtime_id);

    let _ = shutdown_tx.send(());
    let _ = tokio::time::timeout(Duration::from_secs(5), server_task)
        .await
        .expect("server join timeout");
    let _ = shutdown_rx.try_recv();
}
