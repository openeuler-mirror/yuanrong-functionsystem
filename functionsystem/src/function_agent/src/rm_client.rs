use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use tonic::transport::Endpoint;
use yr_proto::internal::runtime_manager_service_client::RuntimeManagerServiceClient;
use yr_proto::internal::{
    RuntimeStatusRequest, SnapshotRequest, StartInstanceRequest, StopInstanceRequest,
};
use yr_runtime_manager::runtime_ops::{
    get_runtime_status_op, snapshot_runtime_op, start_instance_op, stop_instance_op,
};
use yr_runtime_manager::state::RuntimeManagerState;

use crate::config::Config;

#[derive(Clone)]
enum Inner {
    Remote {
        addr: String,
    },
    Local {
        state: Arc<RuntimeManagerState>,
        paths: Vec<String>,
    },
}

#[derive(Clone)]
pub struct RuntimeManagerClient {
    inner: Inner,
}

impl RuntimeManagerClient {
    pub fn remote(addr: String) -> Self {
        Self {
            inner: Inner::Remote { addr },
        }
    }

    pub fn in_process(state: Arc<RuntimeManagerState>, paths: Vec<String>) -> Self {
        Self {
            inner: Inner::Local { state, paths },
        }
    }

    pub fn configured(&self) -> bool {
        match &self.inner {
            Inner::Remote { addr } => !addr.trim().is_empty(),
            Inner::Local { .. } => true,
        }
    }

    async fn connect_remote(
        addr: &str,
    ) -> Result<RuntimeManagerServiceClient<tonic::transport::Channel>, tonic::Status> {
        let uri = Config::normalize_grpc_uri(addr.trim());
        let channel = Endpoint::from_shared(uri)
            .map_err(|e| tonic::Status::internal(e.to_string()))?
            .connect_timeout(Duration::from_secs(5))
            .connect()
            .await
            .map_err(|e| tonic::Status::unavailable(e.to_string()))?;
        Ok(RuntimeManagerServiceClient::new(channel))
    }

    pub async fn start_instance(
        &self,
        instance_id: String,
        function_name: String,
        tenant_id: String,
        runtime_type: String,
        env_vars: HashMap<String, String>,
        resources: HashMap<String, f64>,
        code_path: String,
        config_json: String,
    ) -> Result<yr_proto::internal::StartInstanceResponse, tonic::Status> {
        let req = StartInstanceRequest {
            instance_id,
            function_name,
            tenant_id,
            runtime_type,
            env_vars,
            resources,
            code_path,
            config_json,
        };
        match &self.inner {
            Inner::Local { state, paths } => start_instance_op(state, paths, req),
            Inner::Remote { addr } => {
                if addr.trim().is_empty() {
                    return Err(tonic::Status::failed_precondition(
                        "runtime_manager_address is empty (set --runtime-manager-address or use --enable-merge-process)",
                    ));
                }
                let mut c = Self::connect_remote(addr).await?;
                Ok(c.start_instance(req).await?.into_inner())
            }
        }
    }

    pub async fn stop_instance(
        &self,
        instance_id: String,
        runtime_id: String,
        force: bool,
    ) -> Result<yr_proto::internal::StopInstanceResponse, tonic::Status> {
        let req = StopInstanceRequest {
            instance_id,
            runtime_id,
            force,
        };
        match &self.inner {
            Inner::Local { state, .. } => stop_instance_op(state, req),
            Inner::Remote { addr } => {
                if addr.trim().is_empty() {
                    return Err(tonic::Status::failed_precondition(
                        "runtime_manager_address is empty",
                    ));
                }
                let mut c = Self::connect_remote(addr).await?;
                Ok(c.stop_instance(req).await?.into_inner())
            }
        }
    }

    pub async fn get_runtime_status(
        &self,
        runtime_id: String,
    ) -> Result<yr_proto::internal::RuntimeStatusResponse, tonic::Status> {
        let req = RuntimeStatusRequest { runtime_id };
        match &self.inner {
            Inner::Local { state, .. } => get_runtime_status_op(state, req),
            Inner::Remote { addr } => {
                if addr.trim().is_empty() {
                    return Err(tonic::Status::failed_precondition(
                        "runtime_manager_address is empty",
                    ));
                }
                let mut c = Self::connect_remote(addr).await?;
                Ok(c.get_runtime_status(req).await?.into_inner())
            }
        }
    }

    pub async fn snapshot_runtime(
        &self,
        instance_id: String,
        runtime_id: String,
        snap_type: i32,
    ) -> Result<yr_proto::internal::SnapshotResponse, tonic::Status> {
        let req = SnapshotRequest {
            instance_id,
            runtime_id,
            snap_type,
        };
        match &self.inner {
            Inner::Local { state, .. } => snapshot_runtime_op(state, req),
            Inner::Remote { addr } => {
                if addr.trim().is_empty() {
                    return Err(tonic::Status::failed_precondition(
                        "runtime_manager_address is empty",
                    ));
                }
                let mut c = Self::connect_remote(addr).await?;
                Ok(c.snapshot_runtime(req).await?.into_inner())
            }
        }
    }

    pub async fn readiness_probe(&self) -> bool {
        match &self.inner {
            Inner::Local { .. } => true,
            Inner::Remote { addr } => {
                if addr.trim().is_empty() {
                    return false;
                }
                Self::connect_remote(addr).await.is_ok()
            }
        }
    }
}
