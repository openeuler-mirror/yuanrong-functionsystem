//! Executor-backend + start-path selection — port of C++ `EXECUTOR_TYPE`
//! (`common/constants/constants.h`) and the `SandboxExecutor::StartInstance`
//! dispatch. Pure decision logic so the integration seam in `runtime_ops` stays
//! a thin branch and the choice is unit-testable.

use yr_proto::messages::{RuntimeInstanceInfo, StartInstanceRequest};

/// C++ `enum class EXECUTOR_TYPE { RUNTIME = 0, CONTAINER = 1, UNKNOWN = -1 }`.
pub const EXECUTOR_TYPE_RUNTIME: i32 = 0;
pub const EXECUTOR_TYPE_CONTAINER: i32 = 1;

/// C++ `WarmupType::NONE` (warmup disabled).
pub const WARMUP_TYPE_NONE: i32 = 0;

/// Which runtime_manager backend should handle a StartInstance request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExecutorKind {
    /// Direct process spawn (default; the proven 0.8 lane).
    Runtime,
    /// containerd / sandbox-shim via RuntimeLauncher.
    Container,
}

/// Select the backend from `StartInstanceRequest.type`. Anything other than the
/// explicit CONTAINER value (including the default 0 / RUNTIME) stays on the
/// process backend, so process-mode behavior is unchanged.
pub fn select_executor(req: &StartInstanceRequest) -> ExecutorKind {
    if req.r#type == EXECUTOR_TYPE_CONTAINER {
        ExecutorKind::Container
    } else {
        ExecutorKind::Runtime
    }
}

/// The three CONTAINER start paths (C++ `SandboxExecutor::StartInstance` dispatch).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StartPath {
    Normal,
    WarmUp,
    Restore,
}

/// Pick the start path from the instance info: warmup wins, then a non-empty
/// checkpoint id (restore), else normal cold start. Mirrors the C++ order
/// (warmup → snapshot → normal).
pub fn select_start_path(info: &RuntimeInstanceInfo) -> StartPath {
    if info.warmup_type != WARMUP_TYPE_NONE {
        return StartPath::WarmUp;
    }
    let has_checkpoint = info
        .snapshot_info
        .as_ref()
        .map(|s| !s.checkpoint_id.trim().is_empty())
        .unwrap_or(false);
    if has_checkpoint {
        StartPath::Restore
    } else {
        StartPath::Normal
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use yr_proto::resources::SnapshotInfo;

    #[test]
    fn default_and_runtime_type_select_process_backend() {
        let mut req = StartInstanceRequest::default();
        assert_eq!(select_executor(&req), ExecutorKind::Runtime); // default 0
        req.r#type = EXECUTOR_TYPE_RUNTIME;
        assert_eq!(select_executor(&req), ExecutorKind::Runtime);
        req.r#type = 999; // unknown -> still process (never container)
        assert_eq!(select_executor(&req), ExecutorKind::Runtime);
    }

    #[test]
    fn container_type_selects_sandbox_backend() {
        let req = StartInstanceRequest {
            r#type: EXECUTOR_TYPE_CONTAINER,
            ..Default::default()
        };
        assert_eq!(select_executor(&req), ExecutorKind::Container);
    }

    #[test]
    fn start_path_warmup_wins() {
        let info = RuntimeInstanceInfo {
            warmup_type: 2,
            ..Default::default()
        };
        assert_eq!(select_start_path(&info), StartPath::WarmUp);
    }

    #[test]
    fn start_path_restore_when_checkpoint_present() {
        let info = RuntimeInstanceInfo {
            warmup_type: WARMUP_TYPE_NONE,
            snapshot_info: Some(SnapshotInfo {
                checkpoint_id: "ckpt-1".into(),
                ..Default::default()
            }),
            ..Default::default()
        };
        assert_eq!(select_start_path(&info), StartPath::Restore);
    }

    #[test]
    fn start_path_normal_by_default() {
        let info = RuntimeInstanceInfo::default();
        assert_eq!(select_start_path(&info), StartPath::Normal);
        // empty checkpoint id is not a restore
        let info2 = RuntimeInstanceInfo {
            snapshot_info: Some(SnapshotInfo {
                checkpoint_id: "  ".into(),
                ..Default::default()
            }),
            ..Default::default()
        };
        assert_eq!(select_start_path(&info2), StartPath::Normal);
    }
}
