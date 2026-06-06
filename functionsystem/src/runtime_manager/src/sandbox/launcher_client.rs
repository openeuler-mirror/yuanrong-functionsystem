//! gRPC client for the `runtime.v1.RuntimeLauncher` service over a Unix socket.
//!
//! C++ analogue: the `SandboxExecutor`'s gRPC channel to containerd / sandbox-shim
//! (env `CONTAINER_EP`). containerd and the Go `runtime-launcher` are external
//! components — this is only the client side. The channel is connected lazily and
//! rebuilt on transport failure (C++ parity: 5s connectivity check + auto-reconnect).

use std::sync::Arc;

use anyhow::{anyhow, Context, Result};
use hyper_util::rt::TokioIo;
use tokio::net::UnixStream;
use tokio::sync::Mutex;
use tonic::transport::{Channel, Endpoint, Uri};
use tower::service_fn;

use yr_proto::runtime::v1::runtime_launcher_client::RuntimeLauncherClient;
use yr_proto::runtime::v1::{
    CheckpointRequest, CheckpointResponse, DeleteRequest, DeleteResponse, GetRegisteredRequest,
    GetRegisteredResponse, RegisterRequest, NormalResponse, StartRequest, StartResponse,
    UnregisterRequest, VersionRequest, VersionResponse, WaitRequest, WaitResponse,
};

/// Environment variable carrying the containerd / sandbox-shim UDS endpoint.
pub const CONTAINER_EP_ENV: &str = "CONTAINER_EP";

/// Lazy, reconnecting UDS gRPC client for the RuntimeLauncher service.
#[derive(Clone)]
pub struct LauncherClient {
    uds_path: String,
    channel: Arc<Mutex<Option<Channel>>>,
}

impl LauncherClient {
    pub fn new(uds_path: impl Into<String>) -> Self {
        Self {
            uds_path: uds_path.into(),
            channel: Arc::new(Mutex::new(None)),
        }
    }

    /// Normalize a `CONTAINER_EP` value into a UDS path (strips an optional
    /// `unix://` scheme; rejects empty). Pure helper for testability.
    pub fn parse_endpoint(raw: &str) -> Result<String> {
        let path = raw.strip_prefix("unix://").unwrap_or(raw).trim().to_string();
        if path.is_empty() {
            return Err(anyhow!("{CONTAINER_EP_ENV} is empty"));
        }
        Ok(path)
    }

    /// Build from the `CONTAINER_EP` env var (strips an optional `unix://` scheme).
    pub fn from_env() -> Result<Self> {
        let raw = std::env::var(CONTAINER_EP_ENV)
            .map_err(|_| anyhow!("{CONTAINER_EP_ENV} not set; CONTAINER backend unavailable"))?;
        Ok(Self::new(Self::parse_endpoint(&raw)?))
    }

    pub fn uds_path(&self) -> &str {
        &self.uds_path
    }

    /// Connect a fresh channel to the UDS endpoint (tonic 0.13 over hyper 1.x).
    async fn connect(uds_path: String) -> Result<Channel> {
        // The URI authority is ignored by the custom connector; only the scheme matters.
        let endpoint = Endpoint::try_from("http://[::]:0").context("build endpoint")?;
        let channel = endpoint
            .connect_with_connector(service_fn(move |_: Uri| {
                let uds_path = uds_path.clone();
                async move {
                    let stream = UnixStream::connect(&uds_path).await?;
                    Ok::<_, std::io::Error>(TokioIo::new(stream))
                }
            }))
            .await
            .with_context(|| "connect RuntimeLauncher UDS")?;
        Ok(channel)
    }

    /// Cached channel, connecting on first use.
    async fn channel(&self) -> Result<Channel> {
        let mut guard = self.channel.lock().await;
        if let Some(ch) = guard.as_ref() {
            return Ok(ch.clone());
        }
        let ch = Self::connect(self.uds_path.clone()).await?;
        *guard = Some(ch.clone());
        Ok(ch)
    }

    /// Drop the cached channel so the next call reconnects (C++ auto-reconnect parity).
    pub async fn invalidate(&self) {
        *self.channel.lock().await = None;
    }

    async fn client(&self) -> Result<RuntimeLauncherClient<Channel>> {
        Ok(RuntimeLauncherClient::new(self.channel().await?))
    }

    /// Health probe; used by the background reconnect loop.
    pub async fn version(&self) -> Result<VersionResponse> {
        self.call(|mut c| async move {
            c.version(VersionRequest {
                version: String::new(),
            })
            .await
        })
        .await
    }

    pub async fn start(&self, req: StartRequest) -> Result<StartResponse> {
        self.call(|mut c| async move { c.start(req).await }).await
    }

    pub async fn delete(&self, req: DeleteRequest) -> Result<DeleteResponse> {
        self.call(|mut c| async move { c.delete(req).await }).await
    }

    pub async fn wait(&self, req: WaitRequest) -> Result<WaitResponse> {
        self.call(|mut c| async move { c.wait(req).await }).await
    }

    pub async fn register(&self, req: RegisterRequest) -> Result<NormalResponse> {
        self.call(|mut c| async move { c.register(req).await }).await
    }

    pub async fn unregister(&self, req: UnregisterRequest) -> Result<NormalResponse> {
        self.call(|mut c| async move { c.unregister(req).await })
            .await
    }

    pub async fn get_registered(
        &self,
        req: GetRegisteredRequest,
    ) -> Result<GetRegisteredResponse> {
        self.call(|mut c| async move { c.get_registered(req).await })
            .await
    }

    pub async fn checkpoint(&self, req: CheckpointRequest) -> Result<CheckpointResponse> {
        self.call(|mut c| async move { c.checkpoint(req).await })
            .await
    }

    /// Run one RPC; on transport error invalidate the channel so the next call reconnects.
    async fn call<F, Fut, T>(&self, f: F) -> Result<T>
    where
        F: FnOnce(RuntimeLauncherClient<Channel>) -> Fut,
        Fut: std::future::Future<Output = Result<tonic::Response<T>, tonic::Status>>,
    {
        let client = self.client().await?;
        match f(client).await {
            Ok(resp) => Ok(resp.into_inner()),
            Err(status) => {
                // Transport-level failures warrant a reconnect on the next call.
                if matches!(
                    status.code(),
                    tonic::Code::Unavailable | tonic::Code::Unknown | tonic::Code::Internal
                ) {
                    self.invalidate().await;
                }
                Err(anyhow!("RuntimeLauncher RPC failed: {status}"))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_endpoint_strips_unix_scheme() {
        assert_eq!(
            LauncherClient::parse_endpoint("unix:///run/containerd/cep.sock").unwrap(),
            "/run/containerd/cep.sock"
        );
        assert_eq!(
            LauncherClient::parse_endpoint("  /run/cep.sock  ").unwrap(),
            "/run/cep.sock"
        );
    }

    #[test]
    fn parse_endpoint_rejects_empty() {
        assert!(LauncherClient::parse_endpoint("").is_err());
        assert!(LauncherClient::parse_endpoint("unix://").is_err());
    }

    #[tokio::test]
    async fn connect_fails_cleanly_on_missing_socket() {
        let c = LauncherClient::new("/nonexistent/cep.sock");
        // version() must surface an error (no panic) when the socket is absent.
        assert!(c.version().await.is_err());
    }
}
