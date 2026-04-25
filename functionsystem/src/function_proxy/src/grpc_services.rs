use crate::busproxy::invocation_handler::InboundAction;
use crate::AppContext;
use async_trait::async_trait;
use dashmap::DashMap;
use futures::StreamExt;
use std::pin::Pin;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tokio_stream::Stream;
use tracing::{debug, info, warn};
use yr_proto::bus_service::bus_service_server::BusService;
use yr_proto::bus_service::{
    DiscoverDriverRequest, DiscoverDriverResponse, QueryInstanceRequest, QueryInstanceResponse,
};
use yr_proto::common::ErrorCode;
use yr_proto::exec_service::exec_message::Payload as ExecPayload;
use yr_proto::exec_service::exec_output_data::StreamType as ExecStreamType;
use yr_proto::exec_service::exec_service_server::ExecService;
use yr_proto::exec_service::exec_status_response::Status as ExecSessionStatus;
use yr_proto::exec_service::{
    ExecInputData, ExecMessage, ExecOutputData, ExecResizeRequest, ExecStartRequest,
    ExecStatusResponse,
};
use yr_proto::inner_service::inner_service_server::InnerService;
use yr_proto::inner_service::{
    ForwardCallRequest, ForwardCallResponse, ForwardCallResultRequest, ForwardCallResultResponse,
    ForwardKillRequest, ForwardKillResponse, ForwardRecoverRequest, ForwardRecoverResponse,
    NotifyRequest, NotifyResponse,
};
use yr_proto::runtime_rpc::runtime_rpc_server::RuntimeRpc;
use yr_proto::runtime_rpc::StreamingMessage;

const META_INSTANCE_ID: &str = "instance_id";
const META_INSTANCE_ID_ALT: &str = "instance-id";
const META_RUNTIME_ID: &str = "runtime_id";
const META_RUNTIME_ID_ALT: &str = "runtime-id";

/// gRPC façade for bus-facing and runtime-facing services on the proxy.
pub struct ProxyGrpc {
    ctx: Arc<AppContext>,
}

impl ProxyGrpc {
    pub fn new(ctx: Arc<AppContext>) -> Self {
        Self { ctx }
    }

    fn metadata_instance_id(req: &tonic::Request<impl Sized>) -> Result<String, tonic::Status> {
        let md = req.metadata();
        let v = md
            .get(META_INSTANCE_ID)
            .or_else(|| md.get(META_INSTANCE_ID_ALT))
            .ok_or_else(|| tonic::Status::invalid_argument("missing instance-id gRPC metadata"))?;
        let s = v
            .to_str()
            .map_err(|_| tonic::Status::invalid_argument("instance-id metadata not utf-8"))?;
        if s.trim().is_empty() {
            return Err(tonic::Status::invalid_argument(
                "empty instance-id metadata",
            ));
        }
        Ok(s.to_string())
    }
}

#[async_trait]
impl BusService for ProxyGrpc {
    async fn query_instance(
        &self,
        request: tonic::Request<QueryInstanceRequest>,
    ) -> Result<tonic::Response<QueryInstanceResponse>, tonic::Status> {
        let id = request.into_inner().instance_id;
        let (code, message, status) = match self.ctx.instance_ctrl.get(&id) {
            Some(m) => (
                ErrorCode::ErrNone as i32,
                String::new(),
                m.state.to_string(),
            ),
            None => (
                ErrorCode::ErrInstanceNotFound as i32,
                "instance not on this node".into(),
                String::new(),
            ),
        };
        Ok(tonic::Response::new(QueryInstanceResponse {
            code,
            message,
            status,
        }))
    }

    async fn discover_driver(
        &self,
        request: tonic::Request<DiscoverDriverRequest>,
    ) -> Result<tonic::Response<DiscoverDriverResponse>, tonic::Status> {
        let req = request.into_inner();
        info!(
            driver_ip = %req.driver_ip,
            driver_port = %req.driver_port,
            job_id = %req.job_id,
            instance_id = %req.instance_id,
            "DiscoverDriver received"
        );
        Ok(tonic::Response::new(DiscoverDriverResponse {
            server_version: env!("CARGO_PKG_VERSION").to_string(),
            posix_port: self.ctx.config.posix_port.to_string(),
            node_id: self.ctx.config.node_id.clone(),
            host_ip: self.ctx.config.host.clone(),
        }))
    }
}

#[async_trait]
impl InnerService for ProxyGrpc {
    async fn forward_recover(
        &self,
        request: tonic::Request<ForwardRecoverRequest>,
    ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
        let r = request.into_inner();
        let out = self.ctx.bus.forward_recover(r).await?;
        Ok(tonic::Response::new(out))
    }

    async fn notify_result(
        &self,
        request: tonic::Request<NotifyRequest>,
    ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
        let r = request.into_inner();
        info!(
            request_id = %r.request_id,
            code = r.code,
            "NotifyResult: cross-proxy notification"
        );
        self.ctx.bus.handle_notify_result(r).await?;
        Ok(tonic::Response::new(NotifyResponse {}))
    }

    async fn forward_kill(
        &self,
        request: tonic::Request<ForwardKillRequest>,
    ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
        let r = request.into_inner();
        let out = self.ctx.bus.forward_kill(r).await?;
        Ok(tonic::Response::new(out))
    }

    async fn forward_call_result(
        &self,
        request: tonic::Request<ForwardCallResultRequest>,
    ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
        let r = request.into_inner();
        let out = self.ctx.bus.forward_call_result(r).await?;
        Ok(tonic::Response::new(out))
    }

    async fn forward_call(
        &self,
        request: tonic::Request<ForwardCallRequest>,
    ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
        let r = request.into_inner();
        debug!(instance_id = %r.instance_id, "ForwardCall");
        let out = self.ctx.bus.forward_call(r).await?;
        Ok(tonic::Response::new(out))
    }

    async fn query_instance(
        &self,
        request: tonic::Request<yr_proto::bus_service::QueryInstanceRequest>,
    ) -> Result<tonic::Response<yr_proto::bus_service::QueryInstanceResponse>, tonic::Status> {
        BusService::query_instance(self, request).await
    }
}

pub type MessageStreamOut =
    Pin<Box<dyn Stream<Item = Result<StreamingMessage, tonic::Status>> + Send + 'static>>;

#[async_trait]
impl RuntimeRpc for ProxyGrpc {
    type MessageStreamStream = MessageStreamOut;

    async fn message_stream(
        &self,
        request: tonic::Request<tonic::Streaming<StreamingMessage>>,
    ) -> Result<tonic::Response<Self::MessageStreamStream>, tonic::Status> {
        let md = request.metadata();
        let iid_from_meta = md
            .get(META_INSTANCE_ID)
            .or_else(|| md.get(META_INSTANCE_ID_ALT))
            .and_then(|v| v.to_str().ok())
            .map(|s| s.to_string())
            .filter(|s| !s.trim().is_empty());

        let rid_from_meta = md
            .get(META_RUNTIME_ID)
            .or_else(|| md.get(META_RUNTIME_ID_ALT))
            .and_then(|v| v.to_str().ok())
            .map(|s| s.to_string())
            .filter(|s| !s.trim().is_empty());

        // Runtime connect-back: instance_id matches a known scheduled instance.
        // Driver streams have instance_id starting with "driver-" or unknown.
        let is_runtime_connect_back = iid_from_meta.as_deref().is_some_and(|id| {
            !id.starts_with("driver-") && self.ctx.instance_ctrl.get(id).is_some()
        });

        info!(
            instance_id = ?iid_from_meta,
            runtime_id = ?rid_from_meta,
            is_runtime = is_runtime_connect_back,
            "MessageStream opened"
        );

        let instance_id =
            iid_from_meta.unwrap_or_else(|| format!("driver-{}", uuid::Uuid::new_v4()));

        let mut inbound = request.into_inner();
        let (tx, rx) = mpsc::channel::<Result<StreamingMessage, tonic::Status>>(64);
        let bus = self.ctx.bus.clone();
        self.ctx.bus.attach_runtime_stream(&instance_id, tx.clone());

        if is_runtime_connect_back {
            info!(
                iid = %instance_id,
                runtime_id = ?rid_from_meta,
                "Runtime connected back → notifying driver"
            );
            bus.on_runtime_connected(&instance_id, rid_from_meta.as_deref().unwrap_or(""))
                .await;
        }

        let iid = instance_id.clone();
        tokio::spawn(async move {
            while let Some(item) = inbound.next().await {
                match item {
                    Ok(msg) => {
                        debug!(iid = %iid, message_id = %msg.message_id, body_type = ?msg.body.as_ref().map(|b| std::mem::discriminant(b)), "MessageStream inbound");
                        match bus.handle_runtime_message(&iid, msg).await {
                            InboundAction::Reply(outs) => {
                                for o in outs {
                                    if tx.send(Ok(o)).await.is_err() {
                                        return;
                                    }
                                }
                            }
                            InboundAction::None => {}
                        }
                    }
                    Err(e) => {
                        warn!(iid = %iid, error = %e, "MessageStream inbound error");
                        let _ = tx.send(Err(e)).await;
                        break;
                    }
                }
            }
            info!(iid = %iid, "MessageStream closed");
            if iid.starts_with("driver-") {
                bus.cleanup_driver_stream(&iid).await;
            } else {
                bus.handle_runtime_stream_closed(&iid).await;
            }
        });

        let stream: MessageStreamOut = Box::pin(ReceiverStream::new(rx));
        Ok(tonic::Response::new(stream))
    }
}

struct ExecSession {
    last_io: Instant,
    buf: Vec<u8>,
}

pub type ExecStreamOut =
    Pin<Box<dyn Stream<Item = Result<ExecMessage, tonic::Status>> + Send + 'static>>;

#[async_trait]
impl ExecService for ProxyGrpc {
    type ExecStreamStream = ExecStreamOut;

    async fn exec_stream(
        &self,
        request: tonic::Request<tonic::Streaming<ExecMessage>>,
    ) -> Result<tonic::Response<Self::ExecStreamStream>, tonic::Status> {
        let mut inbound = request.into_inner();
        let (tx, rx) = mpsc::channel::<Result<ExecMessage, tonic::Status>>(64);
        let sessions: Arc<DashMap<String, ExecSession>> = Arc::new(DashMap::new());
        let idle_after = Duration::from_secs(self.ctx.config.exec_session_idle_sec.max(5));
        let sess_for_tick = sessions.clone();
        let tx_tick = tx.clone();
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(1));
            loop {
                interval.tick().await;
                let now = Instant::now();
                let mut dead = Vec::new();
                for e in sess_for_tick.iter() {
                    if now.duration_since(e.last_io) > idle_after {
                        dead.push(e.key().clone());
                    }
                }
                for sid in dead {
                    if let Some((_, _)) = sess_for_tick.remove(&sid) {
                        let _ = tx_tick
                            .send(Ok(ExecMessage {
                                session_id: sid.clone(),
                                payload: Some(ExecPayload::Status(ExecStatusResponse {
                                    status: ExecSessionStatus::Error as i32,
                                    exit_code: -1,
                                    error_message: "session idle timeout".into(),
                                })),
                            }))
                            .await;
                    }
                }
            }
        });

        let tx_in = tx.clone();
        let sessions_in = sessions.clone();
        tokio::spawn(async move {
            while let Some(item) = inbound.next().await {
                match item {
                    Ok(msg) => {
                        let sid = msg.session_id.clone();
                        match &msg.payload {
                            Some(ExecPayload::StartRequest(ExecStartRequest {
                                container_id,
                                command,
                                ..
                            })) => {
                                sessions_in.insert(
                                    sid.clone(),
                                    ExecSession {
                                        last_io: Instant::now(),
                                        buf: Vec::new(),
                                    },
                                );
                                let cmd_hint = command.join(" ");
                                let echo =
                                    format!("started exec on {}: {}\n", container_id, cmd_hint);
                                let _ = tx_in
                                    .send(Ok(ExecMessage {
                                        session_id: sid.clone(),
                                        payload: Some(ExecPayload::Status(ExecStatusResponse {
                                            status: ExecSessionStatus::Started as i32,
                                            exit_code: 0,
                                            error_message: String::new(),
                                        })),
                                    }))
                                    .await;
                                let _ = tx_in
                                    .send(Ok(ExecMessage {
                                        session_id: sid.clone(),
                                        payload: Some(ExecPayload::OutputData(ExecOutputData {
                                            stream_type: ExecStreamType::Stdout as i32,
                                            data: echo.into_bytes(),
                                        })),
                                    }))
                                    .await;
                            }
                            Some(ExecPayload::InputData(ExecInputData { data })) => {
                                if let Some(mut s) = sessions_in.get_mut(&sid) {
                                    s.last_io = Instant::now();
                                    s.buf.extend_from_slice(data);
                                }
                            }
                            Some(ExecPayload::Resize(ExecResizeRequest { rows, cols })) => {
                                if let Some(mut s) = sessions_in.get_mut(&sid) {
                                    s.last_io = Instant::now();
                                }
                                let _ = tx_in
                                    .send(Ok(ExecMessage {
                                        session_id: sid.clone(),
                                        payload: Some(ExecPayload::Status(ExecStatusResponse {
                                            status: ExecSessionStatus::Running as i32,
                                            exit_code: 0,
                                            error_message: format!("resized {}x{}", cols, rows),
                                        })),
                                    }))
                                    .await;
                            }
                            _ => {}
                        }
                    }
                    Err(e) => {
                        let _ = tx_in.send(Err(e)).await;
                        break;
                    }
                }
            }
        });

        let stream: ExecStreamOut = Box::pin(ReceiverStream::new(rx));
        Ok(tonic::Response::new(stream))
    }
}
