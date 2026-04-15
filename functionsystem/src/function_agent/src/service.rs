use std::sync::Arc;

use async_trait::async_trait;
use dashmap::DashMap;
use tonic::{Request, Response, Status};
use tracing::{info, warn};
use yr_proto::internal::function_agent_service_server::FunctionAgentService;
use yr_proto::internal::{
    DeployInstanceRequest, DeployInstanceResponse, StartInstanceRequest, StartInstanceResponse,
    StopInstanceRequest, StopInstanceResponse, UpdateInstanceStatusRequest,
    UpdateInstanceStatusResponse, UpdateResourcesRequest, UpdateResourcesResponse,
};

use crate::deployer::{strip_uri_checksum, DeployContext, DeployMode, DeployRouter};
use crate::registration::SchedulerLink;
use crate::rm_client::RuntimeManagerClient;

pub struct AgentService {
    deploy: Arc<DeployRouter>,
    rm: Arc<RuntimeManagerClient>,
    scheduler: Arc<SchedulerLink>,
    /// instance_id -> deployed code root path
    code_paths: DashMap<String, String>,
    /// instance_id -> runtime_id
    pub runtimes: Arc<DashMap<String, String>>,
}

impl AgentService {
    pub fn new(
        deploy: Arc<DeployRouter>,
        rm: Arc<RuntimeManagerClient>,
        scheduler: Arc<SchedulerLink>,
    ) -> Arc<Self> {
        Arc::new(Self {
            deploy,
            rm,
            scheduler,
            code_paths: DashMap::new(),
            runtimes: Arc::new(DashMap::new()),
        })
    }

    pub fn runtimes_handle(&self) -> Arc<DashMap<String, String>> {
        self.runtimes.clone()
    }
}

#[async_trait]
impl FunctionAgentService for AgentService {
    async fn start_instance(
        &self,
        request: Request<StartInstanceRequest>,
    ) -> Result<Response<StartInstanceResponse>, Status> {
        let r = request.into_inner();
        let code_path = if !r.code_path.trim().is_empty() {
            r.code_path.clone()
        } else if let Some(p) = self.code_paths.get(&r.instance_id) {
            p.clone()
        } else {
            "/tmp".to_string()
        };

        info!(instance_id = %r.instance_id, %code_path, "StartInstance via runtime_manager");

        let resp = self
            .rm
            .start_instance(
                r.instance_id.clone(),
                r.function_name,
                r.tenant_id,
                r.runtime_type,
                r.env_vars,
                r.resources,
                code_path,
                r.config_json,
            )
            .await?;

        if !resp.success {
            return Err(Status::internal(resp.message));
        }
        self.runtimes
            .insert(r.instance_id.clone(), resp.runtime_id.clone());
        Ok(Response::new(resp))
    }

    async fn stop_instance(
        &self,
        request: Request<StopInstanceRequest>,
    ) -> Result<Response<StopInstanceResponse>, Status> {
        let r = request.into_inner();
        let resp = self
            .rm
            .stop_instance(r.instance_id.clone(), r.runtime_id.clone(), r.force)
            .await?;
        if !resp.success {
            return Err(Status::internal(resp.message));
        }
        self.runtimes.remove(&r.instance_id);
        self.code_paths.remove(&r.instance_id);
        Ok(Response::new(resp))
    }

    async fn deploy_instance(
        &self,
        request: Request<DeployInstanceRequest>,
    ) -> Result<Response<DeployInstanceResponse>, Status> {
        let r = request.into_inner();
        let mode = DeployMode::from_proto(&r.deploy_mode);
        let (clean_uri, checksum) = strip_uri_checksum(&r.code_uri);
        let ctx = DeployContext {
            instance_id: &r.instance_id,
            function_name: &r.function_name,
            tenant_id: &r.tenant_id,
            code_uri: clean_uri.as_str(),
            deploy_mode: mode,
            checksum_sha256: checksum,
        };

        match self.deploy.deploy(ctx).await {
            Ok(path) => {
                let s = path.to_string_lossy().to_string();
                if let Err(e) = crate::deployer::record_and_prune_versions(
                    &self.deploy.dest_root,
                    &r.function_name,
                    &r.tenant_id,
                    &path,
                    self.deploy.version_retention,
                ) {
                    warn!(error = %e, "version index / prune skipped");
                }
                self.code_paths.insert(r.instance_id.clone(), s.clone());
                Ok(Response::new(DeployInstanceResponse {
                    success: true,
                    message: String::new(),
                    local_path: s,
                }))
            }
            Err(e) => {
                warn!(error = %e, instance_id = %r.instance_id, "deploy failed");
                Ok(Response::new(DeployInstanceResponse {
                    success: false,
                    message: e.to_string(),
                    local_path: String::new(),
                }))
            }
        }
    }

    async fn update_instance_status(
        &self,
        request: Request<UpdateInstanceStatusRequest>,
    ) -> Result<Response<UpdateInstanceStatusResponse>, Status> {
        let r = request.into_inner();
        self.scheduler.forward_instance_status(&r);
        Ok(Response::new(UpdateInstanceStatusResponse {
            acknowledged: true,
        }))
    }

    async fn update_resources(
        &self,
        request: Request<UpdateResourcesRequest>,
    ) -> Result<Response<UpdateResourcesResponse>, Status> {
        let r = request.into_inner();
        match self.scheduler.forward_update_resources(r).await {
            Ok(success) => Ok(Response::new(UpdateResourcesResponse { success })),
            Err(e) => {
                warn!(error = %e, "forward UpdateResources failed; acknowledging anyway");
                Ok(Response::new(UpdateResourcesResponse { success: true }))
            }
        }
    }
}
