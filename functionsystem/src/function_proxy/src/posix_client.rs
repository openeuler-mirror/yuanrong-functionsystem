//! POSIX / UDS data-plane client (Call / NotifyResult). Control plane hooks for future ops.

use tokio::net::UnixStream;
use tracing::debug;
use yr_proto::runtime_service::NotifyRequest;

/// Sends on a single POSIX stream per process (instance-scoped connections layered on top in C++).
#[derive(Debug)]
pub struct DataInterfacePosixClient {
    uds_path: String,
}

impl DataInterfacePosixClient {
    pub fn new(uds_path: String) -> Self {
        Self { uds_path }
    }

    pub fn path(&self) -> &str {
        &self.uds_path
    }

    /// Deliver async notify bytes on UDS when a path is configured.
    pub async fn notify_result(
        &mut self,
        instance_id: &str,
        n: &NotifyRequest,
    ) -> std::io::Result<()> {
        let path = self.uds_path.trim();
        if path.is_empty() {
            return Ok(());
        }
        let mut stream = UnixStream::connect(path).await?;
        let payload = format!("NOTIFY:{}:{}:{}\n", instance_id, n.request_id, n.message);
        use tokio::io::AsyncWriteExt;
        stream.write_all(payload.as_bytes()).await?;
        stream.flush().await?;
        debug!(%instance_id, path, "posix notify_result sent");
        Ok(())
    }

    /// Binary call frame (placeholder framing; runtime uses structured messages on stream).
    pub async fn call_raw(&mut self, _frame: &[u8]) -> std::io::Result<()> {
        let path = self.uds_path.trim();
        if path.is_empty() {
            return Ok(());
        }
        let mut stream = UnixStream::connect(path).await?;
        use tokio::io::AsyncWriteExt;
        stream.write_all(_frame).await?;
        stream.flush().await?;
        Ok(())
    }
}

/// Reserved for control-plane POSIX operations (deploy signals, etc.).
#[derive(Debug, Default)]
pub struct ControlPlanePosixClient;

impl ControlPlanePosixClient {
    pub async fn ping(&self) -> std::io::Result<()> {
        Ok(())
    }
}
