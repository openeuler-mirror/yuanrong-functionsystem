//! Checkpoint lifecycle for the Restore start path — port of C++
//! `CheckpointOrchestrator`.
//!
//! The actual checkpoint-file transfer/ref-counting lives behind the
//! [`CkptFileManager`] trait (C++ injects a `CkptFileManager`), so the
//! orchestration is unit-testable with a mock and the real downloader (remote
//! storage) is a separable concern.

use std::sync::Arc;

use anyhow::Result;
use async_trait::async_trait;

/// Local checkpoint-file management (download + reference counting).
#[async_trait]
pub trait CkptFileManager: Send + Sync {
    /// Download the checkpoint to a local path, returning that path (= `ckpt_dir`).
    async fn download_checkpoint(&self, checkpoint_id: &str, storage_url: &str) -> Result<String>;
    /// Pin the local checkpoint so it is not GC'd while a runtime is restoring from it.
    async fn add_reference(&self, checkpoint_id: &str, runtime_id: &str) -> Result<()>;
    /// Release the pin held by a runtime (no-op if none).
    async fn release_reference(&self, runtime_id: &str) -> Result<()>;
}

pub struct CheckpointOrchestrator {
    mgr: Arc<dyn CkptFileManager>,
}

impl CheckpointOrchestrator {
    pub fn new(mgr: Arc<dyn CkptFileManager>) -> Self {
        Self { mgr }
    }

    /// Download the checkpoint for a restore; returns the local `ckpt_dir`.
    pub async fn download_for_restore(
        &self,
        checkpoint_id: &str,
        storage_url: &str,
    ) -> Result<String> {
        self.mgr.download_checkpoint(checkpoint_id, storage_url).await
    }

    /// Pin the checkpoint to the runtime. On failure the caller must NOT start.
    pub async fn add_ref(&self, checkpoint_id: &str, runtime_id: &str) -> Result<()> {
        self.mgr.add_reference(checkpoint_id, runtime_id).await
    }

    /// Release the runtime's checkpoint pin (failure-path compensation).
    pub async fn release_ref(&self, runtime_id: &str) -> Result<()> {
        self.mgr.release_reference(runtime_id).await
    }
}

#[cfg(test)]
pub(crate) mod test_support {
    use super::*;
    use parking_lot::Mutex;

    /// In-memory mock CkptFileManager recording add/release for assertions.
    #[derive(Default)]
    pub struct MockCkptFileManager {
        pub fail_download: bool,
        pub fail_add_ref: bool,
        pub added: Mutex<Vec<String>>,
        pub released: Mutex<Vec<String>>,
    }

    #[async_trait]
    impl CkptFileManager for MockCkptFileManager {
        async fn download_checkpoint(&self, checkpoint_id: &str, _url: &str) -> Result<String> {
            if self.fail_download {
                return Err(anyhow::anyhow!("download failed"));
            }
            Ok(format!("/ckpt/{checkpoint_id}"))
        }
        async fn add_reference(&self, _checkpoint_id: &str, runtime_id: &str) -> Result<()> {
            if self.fail_add_ref {
                return Err(anyhow::anyhow!("add_ref failed"));
            }
            self.added.lock().push(runtime_id.to_string());
            Ok(())
        }
        async fn release_reference(&self, runtime_id: &str) -> Result<()> {
            self.released.lock().push(runtime_id.to_string());
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::test_support::MockCkptFileManager;
    use super::*;

    #[tokio::test]
    async fn download_add_release_flow() {
        let mock = Arc::new(MockCkptFileManager::default());
        let orch = CheckpointOrchestrator::new(mock.clone());
        let path = orch.download_for_restore("ckpt-1", "s3://b/o").await.unwrap();
        assert_eq!(path, "/ckpt/ckpt-1");
        orch.add_ref("ckpt-1", "r1").await.unwrap();
        assert_eq!(*mock.added.lock(), vec!["r1".to_string()]);
        orch.release_ref("r1").await.unwrap();
        assert_eq!(*mock.released.lock(), vec!["r1".to_string()]);
    }

    #[tokio::test]
    async fn download_and_add_ref_can_fail() {
        let mock = Arc::new(MockCkptFileManager {
            fail_download: true,
            ..Default::default()
        });
        let orch = CheckpointOrchestrator::new(mock);
        assert!(orch.download_for_restore("c", "u").await.is_err());

        let mock2 = Arc::new(MockCkptFileManager {
            fail_add_ref: true,
            ..Default::default()
        });
        let orch2 = CheckpointOrchestrator::new(mock2);
        assert!(orch2.add_ref("c", "r").await.is_err());
    }
}
