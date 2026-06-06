//! Contract test (M1): drive the `LauncherClient` against a mock `RuntimeLauncher`
//! gRPC server over a real Unix socket. Proves the UDS + tonic + proto wiring
//! end-to-end without needing containerd / the Go runtime-launcher.

use std::sync::Arc;

use tokio::net::UnixListener;
use tokio_stream::wrappers::UnixListenerStream;
use tonic::{transport::Server, Request, Response, Status};

use yr_proto::runtime::v1::runtime_launcher_server::{RuntimeLauncher, RuntimeLauncherServer};
use yr_proto::runtime::v1::{
    CheckpointRequest, CheckpointResponse, DeleteRequest, DeleteResponse, FunctionRuntime,
    GetRegisteredRequest, GetRegisteredResponse, NormalResponse, RegisterRequest, StartRequest,
    StartResponse, UnregisterRequest, VersionRequest, VersionResponse, WaitRequest, WaitResponse,
};
use yr_runtime_manager::sandbox::LauncherClient;

#[derive(Default)]
struct MockLauncher {
    last_start_trace: Arc<parking_lot::Mutex<String>>,
    last_registered: Arc<parking_lot::Mutex<Vec<String>>>,
}

#[tonic::async_trait]
impl RuntimeLauncher for MockLauncher {
    async fn start(&self, req: Request<StartRequest>) -> Result<Response<StartResponse>, Status> {
        let r = req.into_inner();
        *self.last_start_trace.lock() = r.trace_id.clone();
        Ok(Response::new(StartResponse {
            code: 0,
            message: "ok".into(),
            id: format!("sandbox-{}", r.trace_id),
        }))
    }

    async fn delete(&self, _req: Request<DeleteRequest>) -> Result<Response<DeleteResponse>, Status> {
        Ok(Response::new(DeleteResponse {}))
    }

    async fn wait(&self, _req: Request<WaitRequest>) -> Result<Response<WaitResponse>, Status> {
        Ok(Response::new(WaitResponse {
            status: 0,
            exit_code: 0,
            message: String::new(),
        }))
    }

    async fn register(
        &self,
        req: Request<RegisterRequest>,
    ) -> Result<Response<NormalResponse>, Status> {
        let ids: Vec<String> = req.into_inner().func_runtimes.into_iter().map(|f| f.id).collect();
        *self.last_registered.lock() = ids;
        Ok(Response::new(NormalResponse {
            success: true,
            message: String::new(),
        }))
    }

    async fn unregister(
        &self,
        _req: Request<UnregisterRequest>,
    ) -> Result<Response<NormalResponse>, Status> {
        Ok(Response::new(NormalResponse {
            success: true,
            message: String::new(),
        }))
    }

    async fn get_registered(
        &self,
        _req: Request<GetRegisteredRequest>,
    ) -> Result<Response<GetRegisteredResponse>, Status> {
        Ok(Response::new(GetRegisteredResponse {
            func_runtimes: vec![],
        }))
    }

    async fn checkpoint(
        &self,
        _req: Request<CheckpointRequest>,
    ) -> Result<Response<CheckpointResponse>, Status> {
        Ok(Response::new(CheckpointResponse {
            success: true,
            message: String::new(),
        }))
    }

    async fn version(
        &self,
        _req: Request<VersionRequest>,
    ) -> Result<Response<VersionResponse>, Status> {
        Ok(Response::new(VersionResponse::default()))
    }

    // List/Stats are part of the service but unused by these milestones; default-error is fine.
    async fn list(
        &self,
        _req: Request<yr_proto::runtime::v1::ListContainersRequest>,
    ) -> Result<Response<yr_proto::runtime::v1::ListContainersResponse>, Status> {
        Err(Status::unimplemented("list"))
    }

    async fn stats(
        &self,
        _req: Request<yr_proto::runtime::v1::StatsRequest>,
    ) -> Result<Response<yr_proto::runtime::v1::StatsResponse>, Status> {
        Err(Status::unimplemented("stats"))
    }
}

async fn spawn_mock(sock: &str) -> Arc<MockLauncher> {
    let mock = Arc::new(MockLauncher::default());
    let svc = MockLauncher {
        last_start_trace: mock.last_start_trace.clone(),
        last_registered: mock.last_registered.clone(),
    };
    let listener = UnixListener::bind(sock).expect("bind uds");
    let stream = UnixListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(RuntimeLauncherServer::new(svc))
            .serve_with_incoming(stream)
            .await
            .expect("mock server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    mock
}

#[tokio::test]
async fn client_round_trips_start_register_version_over_uds() {
    let sock = format!("/tmp/yr_launcher_mock_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;

    let client = LauncherClient::new(sock.clone());

    // version
    client.version().await.expect("version");

    // start: trace id must reach the server, response id echoes it
    let start = StartRequest {
        trace_id: "trace-xyz".into(),
        ..Default::default()
    };
    let resp = client.start(start).await.expect("start");
    assert_eq!(resp.code, 0);
    assert_eq!(resp.id, "sandbox-trace-xyz");
    assert_eq!(*mock.last_start_trace.lock(), "trace-xyz");

    // register (warmup path): ids must reach the server
    let reg = RegisterRequest {
        func_runtimes: vec![FunctionRuntime {
            id: "warm-1".into(),
            ..Default::default()
        }],
    };
    let nr = client.register(reg).await.expect("register");
    assert!(nr.success);
    assert_eq!(*mock.last_registered.lock(), vec!["warm-1".to_string()]);

    let _ = std::fs::remove_file(&sock);
}
