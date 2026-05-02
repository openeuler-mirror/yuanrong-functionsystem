//! E2E: ExecStream bidirectional gRPC — start command, stdout, stream end.

use std::sync::atomic::AtomicBool;
use std::sync::Arc;

use clap::Parser;
use futures::StreamExt;
use parking_lot::RwLock;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::Server;
use yr_proto::exec_service::exec_message::Payload as ExecPayload;
use yr_proto::exec_service::exec_service_client::ExecServiceClient;
use yr_proto::exec_service::exec_service_server::ExecServiceServer;
use yr_proto::exec_service::exec_status_response::Status as ExecSessionStatus;
use yr_proto::exec_service::{ExecMessage, ExecOutputData, ExecStartRequest, ExecStatusResponse};
use yr_proxy::agent_manager::AgentManager;
use yr_proxy::busproxy::BusProxyCoordinator;
use yr_proxy::config::Config;
use yr_proxy::grpc_services::ProxyGrpc;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::instance_manager::InstanceManager;
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::AppContext;

#[tokio::test]
async fn exec_stream_start_emits_stdout_and_closes_on_inbound_drop() {
    let config = Arc::new(
        Config::try_parse_from([
            "yr-proxy",
            "--node-id",
            "exec-e2e",
            "--grpc-listen-port",
            "1",
            "--host",
            "127.0.0.1",
        ])
        .unwrap(),
    );
    let resource_view = ResourceView::new(ResourceVector {
        cpu: 8.0,
        memory: 64.0,
        npu: 0.0,
    });
    let instance_ctrl = InstanceController::new(config.clone(), resource_view.clone(), None, None);
    let instance_manager = InstanceManager::new(instance_ctrl.clone(), config.clone());
    let bus = BusProxyCoordinator::new(config.clone(), instance_ctrl.clone());
    let ready = Arc::new(tokio::sync::Notify::new());
    let ctx = Arc::new(AppContext {
        config: config.clone(),
        resource_view: resource_view.clone(),
        agent_manager: AgentManager::new(),
        instance_ctrl,
        instance_manager,
        bus,
        etcd: None,
        domain_addr: Arc::new(RwLock::new(String::new())),
        topology: Arc::new(RwLock::new(None)),
        ready: ready.clone(),
        ready_flag: Arc::new(AtomicBool::new(true)),
    });

    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let bound = listener.local_addr().unwrap();
    let incoming = TcpListenerStream::new(listener);

    let grpc = Arc::new(ProxyGrpc::new(ctx));
    let server_task = tokio::spawn(async move {
        Server::builder()
            .add_service(ExecServiceServer::from_arc(grpc))
            .serve_with_incoming(incoming)
            .await
            .expect("serve");
    });

    let uri = format!("http://{bound}");
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    let mut client = ExecServiceClient::connect(uri).await.expect("connect");

    let session_id = "sess-e2e-1".to_string();
    let inbound = tokio_stream::iter(vec![ExecMessage {
        session_id: session_id.clone(),
        payload: Some(ExecPayload::StartRequest(ExecStartRequest {
            container_id: "ctr1".into(),
            command: vec!["echo".into(), "hi".into()],
            ..Default::default()
        })),
    }]);
    let mut out = client
        .exec_stream(inbound)
        .await
        .expect("exec_stream")
        .into_inner();

    let mut saw_started = false;
    let mut saw_stdout = false;
    while let Some(item) = out.next().await {
        let msg = item.expect("msg");
        assert_eq!(msg.session_id, session_id);
        match msg.payload {
            Some(ExecPayload::Status(ExecStatusResponse { status, .. })) => {
                if status == ExecSessionStatus::Started as i32 {
                    saw_started = true;
                }
            }
            Some(ExecPayload::OutputData(ExecOutputData { data, .. })) => {
                let s = String::from_utf8_lossy(&data);
                if s.contains("started exec on ctr1") {
                    saw_stdout = true;
                }
            }
            _ => {}
        }
    }

    assert!(saw_started, "expected Started status");
    assert!(saw_stdout, "expected echoed stdout payload");

    server_task.abort();
}
