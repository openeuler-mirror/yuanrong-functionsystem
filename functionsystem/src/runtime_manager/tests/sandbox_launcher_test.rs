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
use yr_proto::internal::StartInstanceRequest;
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::runtime_ops::{start_instance_op, stop_instance_op};

/// Serializes the CONTAINER_EP set_var -> RuntimeManagerState::new -> remove_var
/// window: parallel tests otherwise race the process-global env and connect each
/// other's mock sockets.
static ENV_LOCK: parking_lot::Mutex<()> = parking_lot::Mutex::new(());
use yr_runtime_manager::sandbox::{
    CheckpointOrchestrator, CkptFileManager, LauncherClient, PortForward, RuntimeStateManager,
    SandboxExecutor, SandboxStartParams,
};
use yr_runtime_manager::state::RuntimeManagerState;
use yr_runtime_manager::Config;

/// Test CkptFileManager: records add/release refs; can force a download failure.
#[derive(Default)]
struct TestCkpt {
    released: Arc<parking_lot::Mutex<Vec<String>>>,
}

#[tonic::async_trait]
impl CkptFileManager for TestCkpt {
    async fn download_checkpoint(&self, id: &str, _url: &str) -> anyhow::Result<String> {
        Ok(format!("/ckpt/{id}"))
    }
    async fn add_reference(&self, _id: &str, _runtime_id: &str) -> anyhow::Result<()> {
        Ok(())
    }
    async fn release_reference(&self, runtime_id: &str) -> anyhow::Result<()> {
        self.released.lock().push(runtime_id.to_string());
        Ok(())
    }
}

#[derive(Default)]
struct MockLauncher {
    last_start_trace: Arc<parking_lot::Mutex<String>>,
    last_start_ports: Arc<parking_lot::Mutex<Vec<String>>>,
    last_registered: Arc<parking_lot::Mutex<Vec<String>>>,
    last_deleted: Arc<parking_lot::Mutex<String>>,
    fail_start: bool,
}

#[tonic::async_trait]
impl RuntimeLauncher for MockLauncher {
    async fn start(&self, req: Request<StartRequest>) -> Result<Response<StartResponse>, Status> {
        let r = req.into_inner();
        *self.last_start_trace.lock() = r.trace_id.clone();
        *self.last_start_ports.lock() = r.ports.clone();
        if self.fail_start {
            return Ok(Response::new(StartResponse {
                code: 1,
                message: "rejected".into(),
                id: String::new(),
            }));
        }
        Ok(Response::new(StartResponse {
            code: 0,
            message: "ok".into(),
            id: format!("sandbox-{}", r.trace_id),
        }))
    }

    async fn delete(&self, req: Request<DeleteRequest>) -> Result<Response<DeleteResponse>, Status> {
        *self.last_deleted.lock() = req.into_inner().id;
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
    spawn_mock_opts(sock, false).await
}

async fn spawn_mock_opts(sock: &str, fail_start: bool) -> Arc<MockLauncher> {
    let mock = Arc::new(MockLauncher {
        fail_start,
        ..Default::default()
    });
    let svc = MockLauncher {
        last_start_trace: mock.last_start_trace.clone(),
        last_start_ports: mock.last_start_ports.clone(),
        last_registered: mock.last_registered.clone(),
        last_deleted: mock.last_deleted.clone(),
        fail_start,
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

fn executor(sock: &str) -> SandboxExecutor {
    let state = Arc::new(RuntimeStateManager::new());
    let ports = Arc::new(SharedPortManager::new(40000, 100).expect("ports"));
    SandboxExecutor::new(state, LauncherClient::new(sock.to_string()), ports)
}

#[tokio::test]
async fn start_normal_allocates_ports_registers_and_forwards_to_launcher() {
    let sock = format!("/tmp/yr_sbx_start_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;
    let exec = executor(&sock);

    let params = SandboxStartParams {
        runtime_id: "r1".into(),
        command: vec!["/runtime".into()],
        trace_id: "t1".into(),
        ..Default::default()
    };
    let forwards = vec![PortForward { container_port: 8080, protocol: "tcp".into() }];
    let started = exec.start_normal(params, forwards).await.expect("start_normal");

    assert_eq!(started.sandbox_id, "sandbox-t1");
    // launcher received exactly one mapping "tcp:<host>:8080"
    let ports = mock.last_start_ports.lock().clone();
    assert_eq!(ports.len(), 1);
    assert!(ports[0].starts_with("tcp:") && ports[0].ends_with(":8080"));
    // state registered with sandbox_id + port mappings json
    let info = exec.state().find("r1").expect("registered");
    assert_eq!(info.sandbox_id, "sandbox-t1");
    assert!(info.port_mappings_json.contains(":8080"));
    assert!(!exec.state().is_start_in_progress("r1")); // committed

    let _ = std::fs::remove_file(&sock);
}

#[tokio::test]
async fn start_normal_rolls_back_on_launcher_rejection() {
    let sock = format!("/tmp/yr_sbx_fail_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let _mock = spawn_mock_opts(&sock, true).await;
    let exec = executor(&sock);

    let params = SandboxStartParams {
        runtime_id: "r2".into(),
        trace_id: "t2".into(),
        ..Default::default()
    };
    let forwards = vec![PortForward { container_port: 9090, protocol: "tcp".into() }];
    let err = exec.start_normal(params, forwards).await;
    assert!(err.is_err(), "launcher code!=0 must fail");
    // SandboxStartGuard rolled back: no sandbox, no in-progress
    assert!(!exec.state().has_sandbox("r2"));
    assert!(!exec.state().is_start_in_progress("r2"));

    let _ = std::fs::remove_file(&sock);
}

#[tokio::test]
async fn stop_deletes_sandbox_releases_ports_and_clears_state() {
    let sock = format!("/tmp/yr_sbx_stop_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;
    let exec = executor(&sock);

    let params = SandboxStartParams {
        runtime_id: "r5".into(),
        trace_id: "t5".into(),
        ..Default::default()
    };
    let forwards = vec![PortForward { container_port: 8080, protocol: "tcp".into() }];
    exec.start_normal(params, forwards).await.expect("start");
    assert!(exec.state().has_sandbox("r5"));

    exec.stop("r5", 5, false).await.expect("stop");
    // launcher.delete called with the sandbox id from state ("sandbox-t5")
    assert_eq!(*mock.last_deleted.lock(), "sandbox-t5");
    // state cleared + forward port released (re-allocatable)
    assert!(!exec.state().has_sandbox("r5"));

    let _ = std::fs::remove_file(&sock);
}

#[tokio::test]
async fn start_warmup_registers_in_pool_and_unregister_removes() {
    let sock = format!("/tmp/yr_sbx_warm_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;
    let exec = executor(&sock);

    let rt = FunctionRuntime {
        id: "w1".into(),
        ..Default::default()
    };
    exec.start_warmup(rt).await.expect("warmup");
    assert_eq!(*mock.last_registered.lock(), vec!["w1".to_string()]);
    assert!(exec.state().is_warm_up("w1"));
    assert!(!exec.state().has_sandbox("w1")); // warmup is orthogonal: no container

    exec.stop_warmup("w1").await.expect("unregister");
    assert!(!exec.state().is_warm_up("w1"));

    let _ = std::fs::remove_file(&sock);
}

#[tokio::test]
async fn start_by_snapshot_sets_ckpt_dir_and_registers_checkpoint() {
    let sock = format!("/tmp/yr_sbx_restore_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let _mock = spawn_mock(&sock).await;
    let released = Arc::new(parking_lot::Mutex::new(vec![]));
    let ckpt = Arc::new(CheckpointOrchestrator::new(Arc::new(TestCkpt {
        released: released.clone(),
    })));
    let state = Arc::new(RuntimeStateManager::new());
    let ports = Arc::new(SharedPortManager::new(41000, 100).unwrap());
    let exec = SandboxExecutor::new(state, LauncherClient::new(sock.clone()), ports)
        .with_checkpoint(ckpt);

    let params = SandboxStartParams {
        runtime_id: "r3".into(),
        trace_id: "t3".into(),
        ..Default::default()
    };
    let started = exec
        .start_by_snapshot(params, "ckpt-9", "s3://b/o", vec![])
        .await
        .expect("restore");
    assert_eq!(started.sandbox_id, "sandbox-t3");
    let info = exec.state().find("r3").expect("registered");
    assert_eq!(info.checkpoint_id, "ckpt-9"); // checkpoint recorded on restore
    assert!(released.lock().is_empty()); // success path: no release

    let _ = std::fs::remove_file(&sock);
}

#[tokio::test]
async fn start_by_snapshot_releases_ref_on_launcher_failure() {
    let sock = format!("/tmp/yr_sbx_restore_fail_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let _mock = spawn_mock_opts(&sock, true).await; // launcher rejects start
    let released = Arc::new(parking_lot::Mutex::new(vec![]));
    let ckpt = Arc::new(CheckpointOrchestrator::new(Arc::new(TestCkpt {
        released: released.clone(),
    })));
    let state = Arc::new(RuntimeStateManager::new());
    let ports = Arc::new(SharedPortManager::new(42000, 100).unwrap());
    let exec = SandboxExecutor::new(state, LauncherClient::new(sock.clone()), ports)
        .with_checkpoint(ckpt);

    let params = SandboxStartParams {
        runtime_id: "r4".into(),
        ..Default::default()
    };
    let res = exec
        .start_by_snapshot(params, "ckpt-x", "s3://b/o", vec![])
        .await;
    assert!(res.is_err());
    assert!(!exec.state().has_sandbox("r4")); // guard rollback
    assert_eq!(*released.lock(), vec!["r4".to_string()]); // ckpt ref released on failure

    let _ = std::fs::remove_file(&sock);
}

// M5 hot-path: start_instance_op routes a CONTAINER config_json to the SandboxExecutor.
#[tokio::test]
async fn start_instance_op_routes_container_config_to_sandbox() {
    let sock = format!("/tmp/yr_rm_container_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;

    // RuntimeManagerState::new builds state.sandbox from CONTAINER_EP.
    let env_guard = ENV_LOCK.lock();
    std::env::set_var("CONTAINER_EP", &sock);
    let log = std::env::temp_dir().join(format!("yr_rm_container_log_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::embedded_in_agent(
        "node-c".into(),
        "http://127.0.0.1:9999".into(),
        "/bin/true".into(),
        40500,
        20,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(SharedPortManager::new(40500, 20).unwrap());
    let state = Arc::new(RuntimeManagerState::new(cfg, ports));
    std::env::remove_var("CONTAINER_EP");
    drop(env_guard);
    assert!(state.sandbox.is_some(), "CONTAINER_EP should build the sandbox backend");

    let req = StartInstanceRequest {
        instance_id: "cinst-1".into(),
        function_name: "fn".into(),
        tenant_id: "t".into(),
        runtime_type: "container".into(),
        env_vars: std::collections::HashMap::new(),
        resources: std::collections::HashMap::new(),
        code_path: String::new(),
        config_json: r#"{"sandbox":true,"image":"aio-yr-runtime:latest","ports":["8080"]}"#.into(),
    };
    let resp = start_instance_op(&state, &[], req).await.expect("container start_instance");
    assert!(resp.success, "container start should succeed");
    assert!(resp.runtime_port > 0, "a host port should be allocated for the 8080 forward");
    // the launcher received a start carrying the forwarded port mapping
    let ports = mock.last_start_ports.lock().clone();
    assert_eq!(ports.len(), 1);
    assert!(ports[0].ends_with(":8080"));

    let _ = std::fs::remove_file(&sock);
}

/// stop_instance_op must route CONTAINER instances (absent from the process map)
/// to SandboxExecutor::stop: launcher Delete called, state + ports cleaned.
#[tokio::test]
async fn stop_instance_op_routes_container_stop_to_sandbox() {
    let sock = format!("/tmp/yr_rm_cstop_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let mock = spawn_mock(&sock).await;

    let env_guard = ENV_LOCK.lock();
    std::env::set_var("CONTAINER_EP", &sock);
    let log = std::env::temp_dir().join(format!("yr_rm_cstop_log_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::embedded_in_agent(
        "node-cs".into(),
        "http://127.0.0.1:9999".into(),
        "/bin/true".into(),
        40550,
        20,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(SharedPortManager::new(40550, 20).unwrap());
    let state = Arc::new(RuntimeManagerState::new(cfg, ports));
    std::env::remove_var("CONTAINER_EP");
    drop(env_guard);

    let req = StartInstanceRequest {
        instance_id: "cstop-1".into(),
        function_name: "fn".into(),
        tenant_id: "t".into(),
        runtime_type: "container".into(),
        env_vars: std::collections::HashMap::new(),
        resources: std::collections::HashMap::new(),
        code_path: String::new(),
        config_json: r#"{"sandbox":true,"image":"img:v1","ports":["8080"]}"#.into(),
    };
    let resp = start_instance_op(&state, &[], req).await.expect("container start");
    assert!(resp.success);
    let rid = resp.runtime_id.clone();
    let sandbox = state.sandbox.as_ref().expect("sandbox backend");
    assert!(sandbox.state().has_sandbox(&rid), "registered after start");

    // Stop by the exact runtime_id (the proxy passes it from metadata).
    let stop = stop_instance_op(
        &state,
        yr_proto::internal::StopInstanceRequest {
            instance_id: "cstop-1".into(),
            runtime_id: rid.clone(),
            force: false,
        },
    )
    .await
    .expect("container stop");
    assert!(stop.success, "container stop should succeed: {}", stop.message);
    assert!(!mock.last_deleted.lock().is_empty(), "launcher Delete called");
    assert!(!sandbox.state().has_sandbox(&rid), "state cleaned after stop");

    let _ = std::fs::remove_file(&sock);
}

/// Stop with a stale runtime_id still resolves the container via the
/// rt-{instance_id}- naming fallback.
#[tokio::test]
async fn stop_instance_op_resolves_container_by_instance_id() {
    let sock = format!("/tmp/yr_rm_cstop2_{}.sock", std::process::id());
    let _ = std::fs::remove_file(&sock);
    let _mock = spawn_mock(&sock).await;

    let env_guard = ENV_LOCK.lock();
    std::env::set_var("CONTAINER_EP", &sock);
    let log = std::env::temp_dir().join(format!("yr_rm_cstop2_log_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::embedded_in_agent(
        "node-cs2".into(),
        "http://127.0.0.1:9999".into(),
        "/bin/true".into(),
        40580,
        20,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(SharedPortManager::new(40580, 20).unwrap());
    let state = Arc::new(RuntimeManagerState::new(cfg, ports));
    std::env::remove_var("CONTAINER_EP");
    drop(env_guard);

    let req = StartInstanceRequest {
        instance_id: "cstop-2".into(),
        function_name: "fn".into(),
        tenant_id: "t".into(),
        runtime_type: "container".into(),
        env_vars: std::collections::HashMap::new(),
        resources: std::collections::HashMap::new(),
        code_path: String::new(),
        config_json: r#"{"sandbox":true,"image":"img:v1"}"#.into(),
    };
    let resp = start_instance_op(&state, &[], req).await.expect("container start");
    let rid = resp.runtime_id.clone();

    let stop = stop_instance_op(
        &state,
        yr_proto::internal::StopInstanceRequest {
            instance_id: "cstop-2".into(),
            runtime_id: "stale".into(),
            force: true,
        },
    )
    .await
    .expect("container stop by instance");
    assert!(stop.success);
    let sandbox = state.sandbox.as_ref().unwrap();
    assert!(!sandbox.state().has_sandbox(&rid));

    let _ = std::fs::remove_file(&sock);
}
