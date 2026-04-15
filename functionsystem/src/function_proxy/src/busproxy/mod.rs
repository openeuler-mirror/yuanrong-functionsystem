//! BusProxy data plane: routes, per-instance dispatch, and peer InnerService forwarding.

use std::sync::Arc;

mod instance_proxy;
mod instance_view;
pub mod invocation_handler;
mod request_dispatcher;
mod request_router;
pub mod service_registry;

pub use instance_view::{InstanceRouteRecord, InstanceView, RouteJson};

use crate::config::Config;
use crate::instance_ctrl::InstanceController;
use crate::posix_client::DataInterfacePosixClient;
use invocation_handler::InvocationHandler;
use parking_lot::RwLock;
use request_router::RouteRetry;
use std::collections::HashMap;
use tokio::sync::{mpsc, Mutex};
use tracing::{debug, info, warn};
use yr_proto::core_service::{self as cs, CallResult, CallResultAck};
use yr_proto::inner_service::inner_service_client::InnerServiceClient;
use yr_proto::inner_service::{
    ForwardCallRequest, ForwardCallResponse, ForwardCallResultRequest, ForwardCallResultResponse,
    ForwardKillRequest, ForwardKillResponse, ForwardRecoverRequest, ForwardRecoverResponse,
};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service::CallRequest;

/// Pending create info stored until the runtime connects back.
#[derive(Clone, Debug)]
struct PendingCreateInfo {
    driver_stream_id: String,
    create_request_id: String,
    function_name: String,
    /// Original args from CreateReq (may contain MetaData for stateful actors).
    create_args: Vec<yr_proto::common::Arg>,
    /// Timestamp when the create was registered (for timeout detection).
    created_at: std::time::Instant,
}

/// Maps instance_id → PendingCreateInfo for notifying the driver on runtime connect-back.
type PendingCreateMap = dashmap::DashMap<String, PendingCreateInfo>;

/// Maps instance_id → driver_stream_id (persistent, used for routing CallResult back to driver).
type InstanceDriverMap = dashmap::DashMap<String, String>;

/// Info stored while waiting for the init CallResult before sending NotifyReq to the driver.
#[derive(Clone, Debug)]
struct PendingInitInfo {
    driver_stream_id: String,
    create_request_id: String,
}

/// Per-instance serialization lock: ensures only one CallReq is in-flight at a time,
/// replicating C++ LiteBus actor mailbox semantics where each actor processes messages
/// strictly one at a time.
type InstanceMailbox = dashmap::DashMap<String, Arc<tokio::sync::Mutex<()>>>;

/// Coordinates runtime streams, etcd routes, and peer gRPC forwards (LiteBus → InnerService).
pub struct BusProxyCoordinator {
    local_node_id: String,
    local_grpc_endpoint: String,
    routes: Arc<RwLock<HashMap<String, InstanceRouteRecord>>>,
    peer_by_node: Arc<RwLock<HashMap<String, String>>>,
    runtime_tx: dashmap::DashMap<String, mpsc::Sender<Result<StreamingMessage, tonic::Status>>>,
    /// Pooled [`InnerServiceClient`] connections keyed by peer gRPC endpoint (`http://host:port`).
    inner_clients: Mutex<HashMap<String, InnerServiceClient<tonic::transport::Channel>>>,
    instance_view: Arc<InstanceView>,
    instance_ctrl: Arc<InstanceController>,
    config: Arc<Config>,
    posix: Arc<Mutex<DataInterfacePosixClient>>,
    retry: RouteRetry,
    pending_creates: PendingCreateMap,
    instance_to_driver: InstanceDriverMap,
    /// Instances waiting for init CallResult before we send NotifyReq to driver.
    pending_inits: dashmap::DashMap<String, PendingInitInfo>,
    /// Per-instance serialization locks: all send paths acquire this before pushing
    /// CallReq/CallResult onto the runtime channel, ensuring strict FIFO ordering
    /// even when multiple tasks (flush_pending, dispatch_local_call, send_to_runtime)
    /// try to send to the same instance concurrently.
    instance_mailbox: InstanceMailbox,
}

impl BusProxyCoordinator {
    pub fn new(
        config: Arc<Config>,
        instance_ctrl: Arc<InstanceController>,
    ) -> Arc<Self> {
        Arc::new(Self {
            local_node_id: config.node_id.clone(),
            local_grpc_endpoint: config.advertise_grpc_endpoint(),
            routes: Arc::new(RwLock::new(HashMap::new())),
            peer_by_node: Arc::new(RwLock::new(HashMap::new())),
            runtime_tx: dashmap::DashMap::new(),
            inner_clients: Mutex::new(HashMap::new()),
            instance_view: Arc::new(InstanceView::new(config.node_id.clone())),
            instance_ctrl,
            posix: Arc::new(Mutex::new(DataInterfacePosixClient::new(
                config.posix_uds_path.clone(),
            ))),
            config,
            retry: RouteRetry::default(),
            pending_creates: dashmap::DashMap::new(),
            instance_to_driver: dashmap::DashMap::new(),
            pending_inits: dashmap::DashMap::new(),
            instance_mailbox: dashmap::DashMap::new(),
        })
    }

    pub fn instance_view(&self) -> &Arc<InstanceView> {
        &self.instance_view
    }

    /// Get or create the per-instance serialization lock.
    fn mailbox(&self, instance_id: &str) -> Arc<tokio::sync::Mutex<()>> {
        self.instance_mailbox
            .entry(instance_id.to_string())
            .or_insert_with(|| Arc::new(tokio::sync::Mutex::new(())))
            .clone()
    }

    pub fn instance_ctrl_ref(&self) -> &Arc<InstanceController> {
        &self.instance_ctrl
    }

    /// Register a pending instance created by a driver's CreateReq.
    /// The instance is allocated an ID and the driver stream is associated.
    pub fn register_pending_instance(
        &self,
        instance_id: &str,
        caller_stream_id: &str,
        create: &cs::CreateRequest,
    ) {
        use crate::state_machine::InstanceMetadata;
        use yr_common::types::InstanceState;

        let meta = InstanceMetadata {
            id: instance_id.to_string(),
            function_name: create.function.clone(),
            tenant: String::new(),
            node_id: self.local_node_id.clone(),
            runtime_id: String::new(),
            runtime_port: 0,
            state: InstanceState::Scheduling,
            created_at_ms: InstanceMetadata::now_ms(),
            updated_at_ms: InstanceMetadata::now_ms(),
            group_id: None,
            trace_id: create.trace_id.clone(),
            resources: Default::default(),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        };
        self.instance_ctrl.insert_metadata(meta);
        self.instance_view.ensure_proxy(instance_id);
        self.instance_view.mark_route_ready(instance_id);

        self.pending_creates.insert(
            instance_id.to_string(),
            PendingCreateInfo {
                driver_stream_id: caller_stream_id.to_string(),
                create_request_id: create.request_id.clone(),
                function_name: create.function.clone(),
                create_args: create.args.clone(),
                created_at: std::time::Instant::now(),
            },
        );
        self.instance_to_driver.insert(
            instance_id.to_string(),
            caller_stream_id.to_string(),
        );

        if let Some(tx) = self.runtime_tx.get(caller_stream_id) {
            self.runtime_tx.insert(instance_id.to_string(), tx.clone());
        }
    }

    pub fn attach_runtime_stream(
        &self,
        instance_id: &str,
        tx: mpsc::Sender<Result<StreamingMessage, tonic::Status>>,
    ) {
        self.runtime_tx.insert(instance_id.to_string(), tx);
        self.instance_view.mark_route_ready(instance_id);
        self.flush_pending(instance_id);
    }

    pub fn detach_runtime_stream(&self, instance_id: &str) {
        self.runtime_tx.remove(instance_id);
        self.instance_view.remove_proxy(instance_id);
        self.instance_to_driver.remove(instance_id);
        self.pending_inits.remove(instance_id);
        self.pending_creates.remove(instance_id);
        self.instance_mailbox.remove(instance_id);
    }

    pub fn upsert_peer_from_json(&self, node_id: &str, value: &[u8]) {
        #[derive(serde::Deserialize)]
        struct Reg {
            #[serde(default)]
            address: Option<String>,
            #[serde(default)]
            grpc: Option<String>,
        }
        if let Ok(r) = serde_json::from_slice::<Reg>(value) {
            let ep = r
                .address
                .or(r.grpc)
                .filter(|s| !s.is_empty());
            if let Some(ep) = ep {
                self.peer_by_node
                    .write()
                    .insert(node_id.to_string(), ep);
            }
        }
    }

    pub fn remove_peer(&self, node_id: &str) {
        self.peer_by_node.write().remove(node_id);
    }

    pub fn apply_instance_route_put(&self, instance_id: &str, value: &[u8]) {
        let rec = match serde_json::from_slice::<RouteJson>(value) {
            Ok(j) => InstanceRouteRecord {
                owner_node_id: j.owner_node().unwrap_or_default(),
                proxy_endpoint: j.endpoint(),
            },
            Err(_) => InstanceRouteRecord::default(),
        };
        self.routes.write().insert(instance_id.to_string(), rec.clone());
        let local = self.is_local_route(&rec, instance_id);
        if local {
            self.instance_view.mark_route_ready(instance_id);
        }
        self.flush_pending(instance_id);
    }

    pub fn apply_instance_route_delete(&self, instance_id: &str) {
        self.routes.write().remove(instance_id);
        self.instance_view.remove_proxy(instance_id);
    }

    fn is_local_route(&self, rec: &InstanceRouteRecord, instance_id: &str) -> bool {
        if !rec.owner_node_id.is_empty() && rec.owner_node_id != self.local_node_id {
            return false;
        }
        self.instance_ctrl.get(instance_id).is_some() || rec.owner_node_id == self.local_node_id
    }

    fn resolve_peer_endpoint(&self, rec: &InstanceRouteRecord) -> Option<String> {
        if let Some(ep) = &rec.proxy_endpoint {
            if !ep.is_empty() {
                return Some(ep.clone());
            }
        }
        if rec.owner_node_id.is_empty() {
            return None;
        }
        self.peer_by_node
            .read()
            .get(&rec.owner_node_id)
            .cloned()
    }

    async fn inner_client(
        &self,
        endpoint: &str,
    ) -> Result<InnerServiceClient<tonic::transport::Channel>, tonic::Status> {
        let mut g = self.inner_clients.lock().await;
        if let Some(c) = g.get(endpoint) {
            return Ok(c.clone());
        }
        let c = InnerServiceClient::connect(endpoint.to_string())
            .await
            .map_err(|e| tonic::Status::unavailable(e.to_string()))?;
        g.insert(endpoint.to_string(), c.clone());
        Ok(c)
    }

    fn flush_pending(&self, instance_id: &str) {
        let Some(px) = self.instance_view.proxies().get(instance_id) else {
            return;
        };
        if !px.dispatcher.route_ready() {
            return;
        }
        let Some(tx_entry) = self.runtime_tx.get(instance_id) else {
            return;
        };
        let tx = tx_entry.clone();
        drop(tx_entry);
        let pending = px.dispatcher.drain();
        if pending.is_empty() {
            return;
        }
        let lock = self.mailbox(instance_id);
        tokio::spawn(async move {
            let _guard = lock.lock().await;
            for p in pending {
                let Some(call) = p.req.req else { continue };
                let msg = StreamingMessage {
                    message_id: uuid::Uuid::new_v4().to_string(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallReq(call)),
                };
                let _ = tx.send(Ok(msg)).await;
            }
        });
    }

    pub async fn forward_call(
        &self,
        req: ForwardCallRequest,
    ) -> Result<ForwardCallResponse, tonic::Status> {
        let instance_id = req.instance_id.clone();
        let call = req
            .req
            .clone()
            .ok_or_else(|| tonic::Status::invalid_argument("missing CallRequest"))?;

        let rid = call.request_id.clone();

        if self.should_dispatch_locally(&instance_id) {
            debug!(
                %instance_id,
                local_endpoint = %self.local_grpc_endpoint,
                "ForwardCall local dispatch"
            );
            self.dispatch_local_call(&instance_id, call).await?;
            return Ok(ForwardCallResponse {
                code: yr_proto::common::ErrorCode::ErrNone as i32,
                message: String::new(),
                request_id: rid,
            });
        }

        let rec = self
            .routes
            .read()
            .get(&instance_id)
            .cloned()
            .unwrap_or_default();
        let endpoint = self.resolve_peer_endpoint(&rec).ok_or_else(|| {
            tonic::Status::failed_precondition("no peer endpoint for forwarded call")
        })?;

        let mut attempt = 0u32;
        loop {
            let mut client = self.inner_client(&endpoint).await?;
            match client.forward_call(req.clone()).await {
                Ok(r) => return Ok(r.into_inner()),
                Err(e) if attempt < self.retry.max_attempts => {
                    attempt += 1;
                    self.retry.backoff(attempt).await;
                    warn!(error = %e, attempt, "forward_call retry");
                }
                Err(e) => return Err(e),
            }
        }
    }

    /// Whether this proxy should handle `instance_id` locally (runtime / instance_ctrl) vs forward
    /// to a peer via [`InnerServiceClient`].
    ///
    /// Etcd instance-route ownership wins over in-memory metadata so a stale local row does not
    /// steal traffic after the route moves to another proxy.
    pub fn should_dispatch_locally(&self, instance_id: &str) -> bool {
        let route = self.routes.read().get(instance_id).cloned();
        if let Some(ref rec) = route {
            if !rec.owner_node_id.is_empty() && rec.owner_node_id != self.local_node_id {
                return false;
            }
        }
        if self.instance_ctrl.get(instance_id).is_some() {
            return true;
        }
        route
            .map(|rec| {
                rec.owner_node_id == self.local_node_id || rec.owner_node_id.is_empty()
            })
            .unwrap_or(false)
    }

    async fn dispatch_local_call(
        &self,
        instance_id: &str,
        call: CallRequest,
    ) -> Result<(), tonic::Status> {
        if let Some(tx) = self.runtime_tx.get(instance_id) {
            let lock = self.mailbox(instance_id);
            let _guard = lock.lock().await;
            let msg = StreamingMessage {
                message_id: uuid::Uuid::new_v4().to_string(),
                meta_data: Default::default(),
                body: Some(streaming_message::Body::CallReq(call)),
            };
            tx.send(Ok(msg))
                .await
                .map_err(|_| tonic::Status::aborted("runtime stream closed"))?;
            return Ok(());
        }

        let px = self.instance_view.ensure_proxy(instance_id);
        px.dispatcher.enqueue(crate::busproxy::request_dispatcher::PendingForward {
            req: ForwardCallRequest {
                req: Some(call),
                instance_id: instance_id.to_string(),
                src_ip: self.config.host.clone(),
                src_node: self.local_node_id.clone(),
            },
        });
        Ok(())
    }

    pub async fn forward_call_result(
        &self,
        req: ForwardCallResultRequest,
    ) -> Result<ForwardCallResultResponse, tonic::Status> {
        let instance_id = req.instance_id.clone();
        let call = req
            .req
            .clone()
            .ok_or_else(|| tonic::Status::invalid_argument("missing CallResult"))?;
        let rid = call.request_id.clone();

        if self.should_dispatch_locally(&instance_id) {
            if let Some(tx) = self.runtime_tx.get(&instance_id) {
                let msg = StreamingMessage {
                    message_id: rid.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallResultReq(call)),
                };
                tx.send(Ok(msg))
                    .await
                    .map_err(|_| tonic::Status::aborted("runtime stream closed"))?;
            }
            return Ok(ForwardCallResultResponse {
                code: yr_proto::common::ErrorCode::ErrNone as i32,
                message: String::new(),
                request_id: rid,
                instance_id,
            });
        }

        let rec = self
            .routes
            .read()
            .get(&instance_id)
            .cloned()
            .unwrap_or_default();
        let endpoint = self
            .resolve_peer_endpoint(&rec)
            .ok_or_else(|| tonic::Status::failed_precondition("no peer for call result"))?;
        let mut attempt = 0u32;
        loop {
            let mut client = self.inner_client(&endpoint).await?;
            match client.forward_call_result(req.clone()).await {
                Ok(r) => return Ok(r.into_inner()),
                Err(e) if attempt < self.retry.max_attempts => {
                    attempt += 1;
                    self.retry.backoff(attempt).await;
                    warn!(error = %e, attempt, "forward_call_result retry");
                }
                Err(e) => return Err(e),
            }
        }
    }

    pub async fn forward_kill(&self, req: ForwardKillRequest) -> Result<ForwardKillResponse, tonic::Status> {
        let id = req.instance_id.clone();
        if !self.should_dispatch_locally(&id) {
            let rec = self.routes.read().get(&id).cloned().unwrap_or_default();
            let endpoint = self
                .resolve_peer_endpoint(&rec)
                .ok_or_else(|| tonic::Status::failed_precondition("no peer for forward_kill"))?;
            let mut attempt = 0u32;
            loop {
                let mut client = self.inner_client(&endpoint).await?;
                match client.forward_kill(req.clone()).await {
                    Ok(r) => return Ok(r.into_inner()),
                    Err(e) if attempt < self.retry.max_attempts => {
                        attempt += 1;
                        self.retry.backoff(attempt).await;
                        warn!(error = %e, attempt, "forward_kill retry");
                    }
                    Err(e) => return Err(e),
                }
            }
        }

        let meta = self.instance_ctrl.get(&id);
        if let Some(ref m) = meta {
            let _ = self
                .instance_ctrl
                .stop_instance(&id, &m.runtime_id, true)
                .await;
        }
        if let Some(mut m) = self.instance_ctrl.instances().get_mut(&id) {
            let _ = m.transition(yr_common::types::InstanceState::Exiting);
            let snap = m.clone();
            drop(m);
            self.instance_ctrl.persist_if_policy(&snap).await;
        }

        Ok(ForwardKillResponse {
            request_id: req.request_id.clone(),
            code: yr_proto::common::ErrorCode::ErrNone as i32,
            message: String::new(),
        })
    }

    /// Handle incoming NotifyResult from a peer proxy.
    ///
    /// C++ equivalent: BaseClient::NotifyResult sends notifyReq on MessageStream
    /// to the local caller instance. In cross-proxy scenarios, the remote proxy
    /// calls this RPC to deliver the notification.
    pub async fn handle_notify_result(
        &self,
        req: yr_proto::inner_service::NotifyRequest,
    ) -> Result<(), tonic::Status> {
        let request_id = &req.request_id;

        // In cross-proxy notifications, request_id is typically the create_request_id,
        // which we can use to look up the instance → driver mapping.
        // Search instance_to_driver for an instance whose create_request_id matches,
        // or directly try request_id as an instance_id key.
        let driver_id = self
            .instance_to_driver
            .get(request_id)
            .map(|e| e.value().clone())
            .or_else(|| {
                self.pending_creates
                    .iter()
                    .find(|e| e.value().create_request_id == *request_id)
                    .and_then(|e| {
                        self.instance_to_driver
                            .get(e.key())
                            .map(|d| d.value().clone())
                    })
            });

        if let Some(did) = driver_id {
            if let Some(tx) = self.runtime_tx.get(&did) {
                let notify = StreamingMessage {
                    message_id: request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::NotifyReq(
                        yr_proto::runtime_service::NotifyRequest {
                            request_id: request_id.clone(),
                            code: req.code,
                            message: req.message.clone(),
                            small_objects: Vec::new(),
                            stack_trace_infos: Vec::new(),
                            runtime_info: None,
                        },
                    )),
                };
                let _ = tx.send(Ok(notify)).await;
                info!(
                    request_id = %request_id,
                    driver = %did,
                    "NotifyResult: forwarded to local driver stream"
                );
                return Ok(());
            }
        }

        warn!(
            request_id = %request_id,
            "NotifyResult: no local driver found for notification"
        );
        Ok(())
    }

    /// Handle ForwardRecover from a peer proxy or from local reconnect.
    ///
    /// C++ equivalent: InstanceCtrlActor::Recover
    /// 1. Re-register the instance metadata if missing
    /// 2. If runtime stream is available, send recoverReq
    /// 3. Transition instance state back to Running on success
    pub async fn forward_recover(
        &self,
        req: ForwardRecoverRequest,
    ) -> Result<ForwardRecoverResponse, tonic::Status> {
        let instance_id = req.instance_id.clone();

        if !self.should_dispatch_locally(&instance_id) {
            let rec = self
                .routes
                .read()
                .get(&instance_id)
                .cloned()
                .unwrap_or_default();
            let endpoint = self
                .resolve_peer_endpoint(&rec)
                .ok_or_else(|| tonic::Status::failed_precondition("no peer for forward_recover"))?;
            let mut attempt = 0u32;
            loop {
                let mut client = self.inner_client(&endpoint).await?;
                match client.forward_recover(req.clone()).await {
                    Ok(r) => return Ok(r.into_inner()),
                    Err(e) if attempt < self.retry.max_attempts => {
                        attempt += 1;
                        self.retry.backoff(attempt).await;
                        warn!(error = %e, attempt, "forward_recover retry");
                    }
                    Err(e) => return Err(e),
                }
            }
        }

        let instance_id = &req.instance_id;
        let runtime_id = &req.runtime_id;

        info!(
            %instance_id,
            %runtime_id,
            runtime_ip = %req.runtime_ip,
            runtime_port = %req.runtime_port,
            function = %req.function,
            "ForwardRecover: rebuilding instance route"
        );

        // Re-insert instance metadata if it's not present (proxy restarted)
        if self.instance_ctrl.get(instance_id).is_none() {
            use crate::state_machine::InstanceMetadata;
            use yr_common::types::InstanceState;

            let meta = InstanceMetadata {
                id: instance_id.to_string(),
                function_name: req.function.clone(),
                tenant: String::new(),
                node_id: self.local_node_id.clone(),
                runtime_id: runtime_id.to_string(),
                runtime_port: req.runtime_port.parse::<i32>().unwrap_or(0),
                state: InstanceState::Running,
                created_at_ms: InstanceMetadata::now_ms(),
                updated_at_ms: InstanceMetadata::now_ms(),
                group_id: None,
                trace_id: String::new(),
                resources: Default::default(),
                etcd_kv_version: None,
                etcd_mod_revision: None,
            };
            self.instance_ctrl.insert_metadata(meta);
            self.instance_view.ensure_proxy(instance_id);
            self.instance_view.mark_route_ready(instance_id);
        }

        // If the runtime's stream is already connected, send a recoverReq
        if let Some(tx) = self.runtime_tx.get(instance_id) {
            let recover_msg = StreamingMessage {
                message_id: format!("recover-{}", instance_id),
                meta_data: Default::default(),
                body: Some(streaming_message::Body::RecoverReq(
                    yr_proto::runtime_service::RecoverRequest {
                        state: Vec::new(),
                        ..Default::default()
                    },
                )),
            };
            let _ = tx.send(Ok(recover_msg)).await;
            info!(%instance_id, "ForwardRecover: sent recoverReq to runtime");
        } else {
            info!(
                %instance_id,
                "ForwardRecover: runtime not yet connected, metadata restored; waiting for connect-back"
            );
        }

        Ok(ForwardRecoverResponse {
            code: yr_proto::common::ErrorCode::ErrNone as i32,
            message: String::new(),
        })
    }

    pub async fn on_runtime_call_result(&self, instance_id: &str, res: CallResult) {
        let ack = StreamingMessage {
            message_id: res.request_id.clone(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallResultAck(CallResultAck {
                code: yr_proto::common::ErrorCode::ErrNone as i32,
                message: String::new(),
            })),
        };
        if let Some(tx) = self.runtime_tx.get(instance_id) {
            let _ = tx.send(Ok(ack)).await;
        }

        // Check if this CallResult is the response to our init CallReq.
        // If so, send NotifyReq to the driver and flush pending invocations.
        if let Some((_, init_info)) = self.pending_inits.remove(instance_id) {
            let code = res.code;
            let msg = &res.message;
            info!(
                %instance_id,
                code,
                message = %msg,
                create_request_id = %init_info.create_request_id,
                "init CallResult received, now sending NotifyReq to driver"
            );

            let target_stream = if !init_info.driver_stream_id.is_empty() {
                init_info.driver_stream_id.clone()
            } else {
                self.runtime_tx.iter()
                    .find(|e| e.key().starts_with("driver-"))
                    .map(|e| e.key().clone())
                    .unwrap_or_default()
            };

            if let Some(tx) = self.runtime_tx.get(&target_stream) {
                let notify_code = if code == 0 {
                    yr_proto::common::ErrorCode::ErrNone as i32
                } else {
                    code
                };
                let notify = StreamingMessage {
                    message_id: init_info.create_request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::NotifyReq(
                        yr_proto::runtime_service::NotifyRequest {
                            request_id: init_info.create_request_id.clone(),
                            code: notify_code,
                            message: res.message.clone(),
                            small_objects: Vec::new(),
                            stack_trace_infos: Vec::new(),
                            runtime_info: None,
                        },
                    )),
                };
                let _ = tx.send(Ok(notify)).await;
                info!(
                    %instance_id,
                    driver = %target_stream,
                    create_request_id = %init_info.create_request_id,
                    "sent NotifyReq to driver after init CallResult"
                );
            }

            self.flush_pending(instance_id);
            return;
        }

        // Normal CallResult: forward to the driver (only via explicit instance_to_driver mapping).
        let driver_id = self
            .instance_to_driver
            .get(instance_id)
            .map(|e| e.value().clone());

        if let Some(did) = driver_id {
            if let Some(tx) = self.runtime_tx.get(&did) {
                let fwd = StreamingMessage {
                    message_id: res.request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallResultReq(res.clone())),
                };
                let _ = tx.send(Ok(fwd)).await;
                info!(%instance_id, driver = %did, request_id = %res.request_id, "forwarded CallResult to driver");
            }
        } else {
            warn!(
                %instance_id,
                request_id = %res.request_id,
                "CallResult: no instance_to_driver mapping, dropping"
            );
        }

        debug!(%instance_id, request_id = %res.request_id, "runtime CallResult handled");
    }

    pub async fn notify_inner(&self, instance_id: &str, n: &yr_proto::runtime_service::NotifyRequest) -> Result<(), tonic::Status> {
        let _ = self
            .posix
            .lock()
            .await
            .notify_result(instance_id, n)
            .await;
        Ok(())
    }

    /// Called when a runtime process connects back to the proxy via MessageStream.
    /// 1) Sends isCreate=true CallRequest to trigger runtime init handler.
    /// 2) Sends NotifyReq to driver to indicate the instance is ready.
    pub async fn on_runtime_connected(&self, instance_id: &str, runtime_id: &str) {
        info!(
            %instance_id,
            %runtime_id,
            "runtime connected back, marking instance ready"
        );

        self.instance_view.mark_route_ready(instance_id);

        if let Some(mut m) = self.instance_ctrl.instances().get_mut(instance_id) {
            if m.runtime_id.is_empty() {
                m.runtime_id = runtime_id.to_string();
            }
        }

        let pending_info = self
            .pending_creates
            .remove(instance_id)
            .map(|(_, v)| v)
            .unwrap_or_else(|| {
                warn!(%instance_id, "no pending create found for runtime connect-back");
                PendingCreateInfo {
                    driver_stream_id: String::new(),
                    create_request_id: instance_id.to_string(),
                    function_name: String::new(),
                    create_args: Vec::new(),
                    created_at: std::time::Instant::now(),
                }
            });

        // Step 1: Send isCreate=true CallRequest to the runtime to trigger init handler.
        // The C++ libruntime requires this before processing any isCreate=false calls.
        // If the driver provided args in CreateReq (stateful actor with MetaData containing
        // InvokeType::CreateInstance), forward those args directly so the Python handler's
        // __create_instance is called. Otherwise, use a default minimal MetaData with
        // InvokeType::CreateInstanceStateless for stateless functions.
        let init_args = if pending_info.create_args.is_empty() {
            let metadata_bytes: Vec<u8> = vec![0x08, 0x02]; // InvokeType::CreateInstanceStateless
            vec![yr_proto::common::Arg {
                value: metadata_bytes,
                ..Default::default()
            }]
        } else {
            info!(
                %instance_id,
                args_count = pending_info.create_args.len(),
                first_arg_bytes = ?pending_info.create_args.first().map(|a| &a.value[..]),
                "using driver-provided CreateReq args for init CallReq (stateful actor)"
            );
            pending_info.create_args.clone()
        };
        let init_call = StreamingMessage {
            message_id: format!("init-{}", instance_id),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallReq(
                yr_proto::runtime_service::CallRequest {
                    function: pending_info.function_name.clone(),
                    args: init_args,
                    trace_id: String::new(),
                    return_object_id: String::new(),
                    is_create: true,
                    sender_id: pending_info.driver_stream_id.clone(),
                    request_id: pending_info.create_request_id.clone(),
                    return_object_i_ds: Vec::new(),
                    create_options: Default::default(),
                    span_id: String::new(),
                },
            )),
        };
        if let Some(tx) = self.runtime_tx.get(instance_id) {
            let lock = self.mailbox(instance_id);
            let _guard = lock.lock().await;
            let _ = tx.send(Ok(init_call)).await;
            info!(
                %instance_id,
                has_driver_args = !pending_info.create_args.is_empty(),
                "sent isCreate=true CallReq to trigger runtime init"
            );
        }

        // Step 2: Defer NotifyReq until init CallResult comes back.
        // The runtime must complete initialization (Python __create_instance for stateful actors)
        // before the driver gets NotifyReq, otherwise the driver may send InvokeReqs to an
        // uninitialized instance.
        self.pending_inits.insert(
            instance_id.to_string(),
            PendingInitInfo {
                driver_stream_id: pending_info.driver_stream_id.clone(),
                create_request_id: pending_info.create_request_id.clone(),
            },
        );
        info!(
            %instance_id,
            create_request_id = %pending_info.create_request_id,
            "deferred NotifyReq until init CallResult arrives"
        );
    }

    /// Schedule an instance via yr-agent's FunctionAgentService (StartInstance).
    /// Called from CreateReq handler to trigger the real scheduling path:
    /// proxy → agent → embedded runtime_manager → fork runtime process.
    pub async fn schedule_instance_via_agent(
        &self,
        instance_id: &str,
        function_name: &str,
        _trace_id: &str,
    ) -> Result<(String, i32), tonic::Status> {
        let resources = self.instance_ctrl.clamp_resources(&Default::default());
        self.instance_ctrl
            .start_instance(instance_id, function_name, "", resources, "default")
            .await
    }

    /// Spawn a background reaper that periodically checks for timed-out pending creates.
    /// If a runtime doesn't connect back within `timeout`, sends a failure NotifyReq to the
    /// driver and transitions the instance to Exiting.
    pub fn spawn_pending_create_reaper(self: &Arc<Self>, timeout: std::time::Duration) {
        let bus = Arc::clone(self);
        let interval = std::cmp::max(timeout / 4, std::time::Duration::from_secs(1));
        tokio::spawn(async move {
            let mut tick = tokio::time::interval(interval);
            loop {
                tick.tick().await;
                let now = std::time::Instant::now();
                let expired: Vec<(String, PendingCreateInfo)> = bus
                    .pending_creates
                    .iter()
                    .filter(|e| now.duration_since(e.value().created_at) > timeout)
                    .map(|e| (e.key().clone(), e.value().clone()))
                    .collect();

                for (iid, pending) in expired {
                    if bus.pending_creates.remove(&iid).is_none() {
                        continue;
                    }
                    warn!(
                        instance_id = %iid,
                        timeout_secs = timeout.as_secs(),
                        "runtime connect-back timed out, failing create"
                    );

                    let target_stream = &pending.driver_stream_id;
                    if let Some(tx) = bus.runtime_tx.get(target_stream) {
                        let notify = StreamingMessage {
                            message_id: pending.create_request_id.clone(),
                            meta_data: Default::default(),
                            body: Some(streaming_message::Body::NotifyReq(
                                yr_proto::runtime_service::NotifyRequest {
                                    request_id: pending.create_request_id.clone(),
                                    code: yr_proto::common::ErrorCode::ErrInnerSystemError as i32,
                                    message: format!(
                                        "runtime connect-back timed out after {}s",
                                        timeout.as_secs()
                                    ),
                                    small_objects: Vec::new(),
                                    stack_trace_infos: Vec::new(),
                                    runtime_info: None,
                                },
                            )),
                        };
                        let _ = tx.send(Ok(notify)).await;
                    }

                    bus.pending_inits.remove(&iid);

                    if let Some(mut m) = bus.instance_ctrl.instances().get_mut(&iid) {
                        let _ = m.transition(yr_common::types::InstanceState::Exiting);
                    }
                }
            }
        });
    }

    /// Execute a kill on an instance: stop the runtime process, transition state, clean up.
    ///
    /// C++ equivalent: InstanceCtrlActor::HandleKill + SignalRoute.
    /// If the instance is local, stops the runtime and transitions to Exiting.
    /// If remote, forwards via InnerService.ForwardKill.
    pub async fn execute_kill(&self, instance_id: &str, signal: i32) -> (i32, String) {
        if self.should_dispatch_locally(instance_id) {
            let meta = self.instance_ctrl.get(instance_id);
            if let Some(ref m) = meta {
                if let Err(e) = self
                    .instance_ctrl
                    .stop_instance(instance_id, &m.runtime_id, signal != 2)
                    .await
                {
                    warn!(
                        %instance_id,
                        error = %e,
                        "execute_kill: stop_instance failed"
                    );
                }
            }

            if let Some(mut m) = self.instance_ctrl.instances().get_mut(instance_id) {
                let _ = m.transition(yr_common::types::InstanceState::Exiting);
                let snap = m.clone();
                drop(m);
                self.instance_ctrl.persist_if_policy(&snap).await;
            }

            self.detach_runtime_stream(instance_id);

            info!(%instance_id, signal, "execute_kill: local kill completed");
            (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
        } else {
            let req = ForwardKillRequest {
                request_id: uuid::Uuid::new_v4().to_string(),
                src_instance_id: String::new(),
                req: Some(cs::KillRequest {
                    instance_id: instance_id.to_string(),
                    signal,
                    ..Default::default()
                }),
                instance_id: instance_id.to_string(),
                ..Default::default()
            };
            match self.forward_kill(req).await {
                Ok(r) => (r.code, r.message),
                Err(e) => {
                    warn!(%instance_id, error = %e, "execute_kill: forward_kill failed");
                    (
                        yr_proto::common::ErrorCode::ErrInnerSystemError as i32,
                        e.message().to_string(),
                    )
                }
            }
        }
    }

    /// Forward a user-defined signal (≥64) to a target instance's MessageStream.
    /// In C++, these are routed to the target's stream via SignalRoute→SendSignal.
    pub async fn forward_user_signal(
        &self,
        target_id: &str,
        caller_id: &str,
        kill: &yr_proto::core_service::KillRequest,
    ) -> (i32, String) {
        if let Some(tx) = self.runtime_tx.get(target_id) {
            let fwd = yr_proto::runtime_rpc::StreamingMessage {
                message_id: uuid::Uuid::new_v4().to_string(),
                meta_data: Default::default(),
                body: Some(yr_proto::runtime_rpc::streaming_message::Body::KillReq(
                    kill.clone(),
                )),
            };
            let _ = tx.send(Ok(fwd)).await;
            (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
        } else if !self.should_dispatch_locally(target_id) {
            let req = ForwardKillRequest {
                request_id: uuid::Uuid::new_v4().to_string(),
                src_instance_id: caller_id.to_string(),
                req: Some(kill.clone()),
                instance_id: target_id.to_string(),
                ..Default::default()
            };
            match self.forward_kill(req).await {
                Ok(r) => (r.code, r.message),
                Err(e) => {
                    warn!(target = %target_id, error = %e, "forward_user_signal: peer forward failed");
                    (yr_proto::common::ErrorCode::ErrInnerSystemError as i32, e.message().to_string())
                }
            }
        } else {
            warn!(
                target = %target_id,
                caller = %caller_id,
                signal = kill.signal,
                "forward_user_signal: no stream for local target"
            );
            (yr_proto::common::ErrorCode::ErrInstanceNotFound as i32,
             format!("no runtime stream for instance {target_id}"))
        }
    }

    /// Apply an exit event to a local instance: transition state and release resources.
    pub async fn apply_instance_exit(&self, instance_id: &str, ok: bool, message: &str) {
        self.instance_ctrl
            .apply_exit_event(instance_id, ok, message)
            .await;
        self.detach_runtime_stream(instance_id);
    }

    pub fn has_runtime_stream(&self, instance_id: &str) -> bool {
        self.runtime_tx.contains_key(instance_id)
    }

    pub async fn send_to_runtime(&self, instance_id: &str, msg: StreamingMessage) {
        if let Some(tx) = self.runtime_tx.get(instance_id) {
            let lock = self.mailbox(instance_id);
            let _guard = lock.lock().await;
            let body_type = msg.body.as_ref().map(|b| std::mem::discriminant(b));
            match tx.send(Ok(msg)).await {
                Ok(()) => info!(%instance_id, body = ?body_type, "sent message to runtime stream"),
                Err(_) => warn!(%instance_id, "runtime stream closed, failed to send"),
            }
        } else {
            warn!(%instance_id, "no runtime stream found for send_to_runtime");
        }
    }

    pub async fn handle_runtime_message(
        &self,
        instance_id: &str,
        msg: StreamingMessage,
    ) -> invocation_handler::InboundAction {
        InvocationHandler::handle_runtime_inbound(instance_id, msg, self).await
    }
}
