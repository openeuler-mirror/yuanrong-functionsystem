//! HTTP `/healthy` and `/readiness` behavior.

use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use tokio::net::TcpListener;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::Server;
use tower::ServiceExt;
use yr_agent::http_api::{router, HealthState};
use yr_agent::node_manager::NodeManager;
use yr_agent::registration::SchedulerLink;
use yr_agent::rm_client::RuntimeManagerClient;
use yr_proto::internal::local_scheduler_service_server::{LocalSchedulerService, LocalSchedulerServiceServer};
use yr_proto::internal::{
    EvictInstancesRequest, EvictInstancesResponse, GroupScheduleRequest, GroupScheduleResponse,
    KillGroupRequest, KillGroupResponse, PreemptInstancesRequest, PreemptInstancesResponse,
    ScheduleRequest, ScheduleResponse,
};

#[derive(Clone, Default)]
struct OkLocalScheduler;

#[async_trait]
impl LocalSchedulerService for OkLocalScheduler {
    async fn schedule(
        &self,
        _request: tonic::Request<ScheduleRequest>,
    ) -> Result<tonic::Response<ScheduleResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("schedule"))
    }

    async fn evict_instances(
        &self,
        _request: tonic::Request<EvictInstancesRequest>,
    ) -> Result<tonic::Response<EvictInstancesResponse>, tonic::Status> {
        Ok(tonic::Response::new(EvictInstancesResponse {
            success: true,
            evicted_ids: vec![],
        }))
    }

    async fn preempt_instances(
        &self,
        _request: tonic::Request<PreemptInstancesRequest>,
    ) -> Result<tonic::Response<PreemptInstancesResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("preempt"))
    }

    async fn group_schedule(
        &self,
        _request: tonic::Request<GroupScheduleRequest>,
    ) -> Result<tonic::Response<GroupScheduleResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("group_schedule"))
    }

    async fn kill_group(
        &self,
        _request: tonic::Request<KillGroupRequest>,
    ) -> Result<tonic::Response<KillGroupResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("kill_group"))
    }
}

fn in_process_rm() -> Arc<RuntimeManagerClient> {
    let log = std::env::temp_dir().join(format!("yr_agent_probe_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(yr_runtime_manager::Config::embedded_in_agent(
        "nid".into(),
        "http://127.0.0.1:1".into(),
        "/bin/true".into(),
        40200,
        10,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(
        yr_runtime_manager::port_manager::SharedPortManager::new(40200, 10).unwrap(),
    );
    let st = Arc::new(yr_runtime_manager::state::RuntimeManagerState::new(cfg, ports));
    Arc::new(RuntimeManagerClient::in_process(
        st,
        vec!["/bin/true".to_string()],
    ))
}

async fn start_mock_local_scheduler() -> (String, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();
    let uri = format!("http://{}", addr);
    let incoming = TcpListenerStream::new(listener);
    let server = Server::builder()
        .add_service(LocalSchedulerServiceServer::new(OkLocalScheduler))
        .serve_with_incoming(incoming);
    let h = tokio::spawn(async move {
        let _ = server.await;
    });
    tokio::time::sleep(Duration::from_millis(30)).await;
    (uri, h)
}

#[tokio::test]
async fn healthy_ok_with_matching_node_id_and_pid() {
    let rm = in_process_rm();
    let sched = SchedulerLink::new_arc(
        "http://127.0.0.1:9".into(),
        "node-x".into(),
        "http://127.0.0.1:1".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = router(HealthState {
        rm,
        scheduler: sched,
        node_id: "node-x".into(),
        node,
    });
    let pid = std::process::id().to_string();
    let res = app
        .oneshot(
            Request::builder()
                .uri("/healthy")
                .header("node-id", "node-x")
                .header("pid", pid)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}

#[tokio::test]
async fn healthy_rejects_bad_node_id() {
    let rm = in_process_rm();
    let sched = SchedulerLink::new_arc(
        "http://127.0.0.1:9".into(),
        "node-x".into(),
        "http://127.0.0.1:1".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = router(HealthState {
        rm,
        scheduler: sched,
        node_id: "node-x".into(),
        node,
    });
    let res = app
        .oneshot(
            Request::builder()
                .uri("/healthy")
                .header("node-id", "other")
                .header("pid", std::process::id().to_string())
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
    let body = res.into_body().collect().await.unwrap().to_bytes();
    assert_eq!(body.as_ref(), b"error nodeID");
}

#[tokio::test]
async fn healthy_rejects_bad_pid() {
    let rm = in_process_rm();
    let sched = SchedulerLink::new_arc(
        "http://127.0.0.1:9".into(),
        "node-x".into(),
        "http://127.0.0.1:1".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = router(HealthState {
        rm,
        scheduler: sched,
        node_id: "node-x".into(),
        node,
    });
    let res = app
        .oneshot(
            Request::builder()
                .uri("/healthy")
                .header("node-id", "node-x")
                .header("pid", "1")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
    let body = res.into_body().collect().await.unwrap().to_bytes();
    assert_eq!(body.as_ref(), b"error PID");
}

#[tokio::test]
async fn readiness_ok_when_rm_local_and_scheduler_reachable() {
    let (uri, _h) = start_mock_local_scheduler().await;
    let rm = in_process_rm();
    let sched = SchedulerLink::new_arc(
        uri,
        "n1".into(),
        "http://127.0.0.1:22799".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = router(HealthState {
        rm,
        scheduler: sched,
        node_id: "n1".into(),
        node,
    });
    let res = app
        .oneshot(Request::builder().uri("/readiness").body(Body::empty()).unwrap())
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::OK);
    let body = res.into_body().collect().await.unwrap().to_bytes();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["ready"], true);
    assert_eq!(v["runtime_manager"], true);
    assert_eq!(v["scheduler"], true);
    assert_eq!(v["node_ready_flag"], true);
}

#[tokio::test]
async fn readiness_fails_when_scheduler_unreachable() {
    let rm = in_process_rm();
    let sched = SchedulerLink::new_arc(
        "http://127.0.0.1:7".into(),
        "n1".into(),
        "http://127.0.0.1:22799".into(),
    );
    let node = NodeManager::new_arc();
    node.set_ready(true);
    let app = router(HealthState {
        rm,
        scheduler: sched,
        node_id: "n1".into(),
        node,
    });
    let res = app
        .oneshot(Request::builder().uri("/readiness").body(Body::empty()).unwrap())
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
    let body = res.into_body().collect().await.unwrap().to_bytes();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["ready"], false);
    assert_eq!(v["scheduler"], false);
}
