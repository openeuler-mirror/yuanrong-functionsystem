//! e2e probe for the sandbox secondary paths (warmup / checkpoint-restore)
//! against the REAL runtime-launcher (CONTAINER_EP) — run inside the AIO:
//!
//!   CONTAINER_EP=unix:///var/run/runtime-launcher.sock ./sandbox_probe
//!
//! Exercises: Register/Unregister (warm-up pool) and Start-with-ckpt_dir
//! (restore validation + container boot) + Delete. Exits non-zero on failure.

use std::collections::HashMap;
use std::sync::Arc;

use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::sandbox::{
    build_start_request, LauncherClient, RuntimeStateManager, SandboxExecutor, SandboxStartParams,
};

#[tokio::main(flavor = "current_thread")]
async fn main() -> anyhow::Result<()> {
    let launcher = LauncherClient::from_env()?;
    let state = Arc::new(RuntimeStateManager::new());
    let ports = Arc::new(SharedPortManager::new(45000, 16)?);
    let exec = SandboxExecutor::new(state.clone(), launcher.clone(), ports);

    // ── warm-up path: Register → pool membership → Unregister ──────────────
    let warm = yr_proto::runtime::v1::FunctionRuntime {
        id: "probe-warm-1".into(),
        command: vec!["sleep".into(), "5".into()],
        ..Default::default()
    };
    exec.start_warmup(warm).await?;
    anyhow::ensure!(state.is_warm_up("probe-warm-1"), "warm pool membership");
    println!("WARMUP register OK");
    exec.stop_warmup("probe-warm-1").await?;
    anyhow::ensure!(!state.is_warm_up("probe-warm-1"), "warm pool cleared");
    println!("WARMUP unregister OK");

    // ── restore path: Start with ckpt_dir (real launcher validates the dir,
    //    then boots the container from the checkpoint) ───────────────────────
    let ckpt_dir = "/tmp/probe-ckpt";
    std::fs::create_dir_all(ckpt_dir)?;
    let params = SandboxStartParams {
        runtime_id: "probe-restore-1".into(),
        command: vec!["sleep".into(), "30".into()],
        rootfs: Some(yr_runtime_manager::sandbox::RootfsSpec::Image(
            "aio-yr-runtime:latest".into(),
        )),
        ckpt_dir: ckpt_dir.into(),
        user_envs: HashMap::new(),
        ..Default::default()
    };
    let req = build_start_request(&params);
    let resp = launcher.start(req).await?;
    anyhow::ensure!(resp.code == 0, "restore start failed: {} {}", resp.code, resp.message);
    let sandbox_id = resp.id.clone();
    println!("RESTORE start OK sandbox_id={sandbox_id}");

    // negative control: nonexistent ckpt_dir must be rejected
    let mut bad = params.clone();
    bad.runtime_id = "probe-restore-bad".into();
    bad.ckpt_dir = "/tmp/does-not-exist-ckpt".into();
    let bad_resp = launcher.start(build_start_request(&bad)).await;
    let rejected = match bad_resp {
        Err(_) => true,
        Ok(r) => r.code != 0,
    };
    anyhow::ensure!(rejected, "missing ckpt_dir must be rejected");
    println!("RESTORE missing-dir rejection OK");

    // cleanup
    launcher
        .delete(yr_proto::runtime::v1::DeleteRequest {
            id: sandbox_id,
            timeout: 0,
        })
        .await?;
    println!("CLEANUP delete OK");
    println!("SANDBOX_PROBE PASS");
    Ok(())
}
