use crate::runtime_ops::{
    get_runtime_status_op, snapshot_runtime_op, start_instance_op, stop_instance_op,
};
use crate::state::RuntimeManagerState;
use async_trait::async_trait;
use std::sync::Arc;
use tonic::{Request, Response, Status};
use yr_proto::internal::runtime_manager_service_server::RuntimeManagerService;
use yr_proto::internal::{
    RuntimeStatusRequest, RuntimeStatusResponse, SnapshotRequest, SnapshotResponse,
    StartInstanceRequest, StartInstanceResponse, StopInstanceRequest, StopInstanceResponse,
};

pub struct RuntimeManagerGrpc {
    state: Arc<RuntimeManagerState>,
    paths: Vec<String>,
}

impl RuntimeManagerGrpc {
    pub fn new(config: Arc<crate::config::Config>, state: Arc<RuntimeManagerState>) -> Self {
        let paths = config.runtime_path_list();
        Self { state, paths }
    }
}

#[async_trait]
impl RuntimeManagerService for RuntimeManagerGrpc {
    async fn start_instance(
        &self,
        request: Request<StartInstanceRequest>,
    ) -> Result<Response<StartInstanceResponse>, Status> {
        let req = request.into_inner();
        let resp = start_instance_op(&self.state, &self.paths, req)?;
        Ok(Response::new(resp))
    }

    async fn stop_instance(
        &self,
        request: Request<StopInstanceRequest>,
    ) -> Result<Response<StopInstanceResponse>, Status> {
        let req = request.into_inner();
        let resp = stop_instance_op(&self.state, req)?;
        Ok(Response::new(resp))
    }

    async fn snapshot_runtime(
        &self,
        request: Request<SnapshotRequest>,
    ) -> Result<Response<SnapshotResponse>, Status> {
        let req = request.into_inner();
        let resp = snapshot_runtime_op(&self.state, req)?;
        Ok(Response::new(resp))
    }

    async fn get_runtime_status(
        &self,
        request: Request<RuntimeStatusRequest>,
    ) -> Result<Response<RuntimeStatusResponse>, Status> {
        let req = request.into_inner();
        let resp = get_runtime_status_op(&self.state, req)?;
        Ok(Response::new(resp))
    }
}
