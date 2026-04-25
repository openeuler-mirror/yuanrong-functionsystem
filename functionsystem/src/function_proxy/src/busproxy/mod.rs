//! BusProxy data plane: routes, per-instance dispatch, and peer InnerService forwarding.

use prost::Message;
use std::fs;
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
    /// Original CreateReq options (e.g. need_order, graceful shutdown, etc).
    create_options: HashMap<String, String>,
    /// Timestamp when the create was registered (for timeout detection).
    created_at: std::time::Instant,
}

/// Durable launch metadata needed to restart a runtime for RecoverRetryTimes.
#[derive(Clone, Debug)]
struct ActiveInstanceInfo {
    driver_stream_id: String,
    function_name: String,
    trace_id: String,
    create_options: HashMap<String, String>,
    create_args: Vec<yr_proto::common::Arg>,
    remaining_recoveries: i32,
    recovering: bool,
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

fn recover_retry_times(create_options: &HashMap<String, String>) -> i32 {
    create_options
        .get("RecoverRetryTimes")
        .or_else(|| create_options.get("RECOVER_RETRY_TIMES"))
        .and_then(|v| v.trim().parse::<i32>().ok())
        .unwrap_or(0)
        .max(0)
}

fn graceful_shutdown_seconds(create_options: &HashMap<String, String>) -> u64 {
    create_options
        .get("GRACEFUL_SHUTDOWN_TIME")
        .or_else(|| create_options.get("GracefulShutdownTime"))
        .and_then(|v| v.trim().parse::<u64>().ok())
        .filter(|v| *v > 0)
        .unwrap_or(60)
}

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
    instance_to_job: dashmap::DashMap<String, String>,
    /// CallResult routing table keyed by request_id. Driver-origin invokes route
    /// back to the driver stream; nested runtime-origin invokes route back to
    /// the source runtime stream.
    result_to_caller: dashmap::DashMap<String, String>,
    /// Preserve the original outer StreamingMessage.message_id for a request_id.
    request_to_message_id: dashmap::DashMap<String, String>,
    /// Preserve the original CallRequest so in-flight ordered calls can be replayed after recovery.
    request_to_call: dashmap::DashMap<String, CallRequest>,
    /// Ordered actor invoke sequence keyed by request_id.
    request_to_sequence: dashmap::DashMap<String, i64>,
    /// Next expected ordered invoke sequence per instance.
    instance_next_sequence: dashmap::DashMap<String, i64>,
    /// Requests that have actually been sent to runtime and are waiting for result.
    dispatched_request_to_instance: dashmap::DashMap<String, String>,
    /// When a direct CallResult is forwarded to a driver/runtime as NotifyReq, the
    /// consumer's NotifyRsp must be translated back to CallResultAck for the source runtime.
    notify_ack_to_runtime: dashmap::DashMap<String, String>,
    /// Tracks which target instance currently owns a given request_id so stream
    /// close can fail outstanding requests instead of hanging callers forever.
    request_to_instance: dashmap::DashMap<String, String>,
    /// CallRsp routing table keyed by outer StreamingMessage.message_id.
    call_ack_to_caller: dashmap::DashMap<String, String>,
    /// Lightweight in-proxy checkpoint store for actor save/load/recover flows.
    state_snapshots: dashmap::DashMap<String, Vec<u8>>,
    /// Launch metadata for currently owned instances. Unlike pending_creates,
    /// this survives the initial create and is used for abnormal runtime restart.
    active_instances: dashmap::DashMap<String, ActiveInstanceInfo>,
    /// Instances with a RecoverReq in flight; queued invokes flush only after RecoverRsp.
    pending_recovers: dashmap::DashMap<String, ()>,
    /// Owners/groups whose in-flight GroupCreate should stop scheduling new instances.
    /// This mirrors C++ range/group termination: once Terminate is requested, an
    /// already-running batch scheduler must not keep launching children.
    cancelled_group_creates: dashmap::DashMap<String, ()>,
    /// Instances waiting for init CallResult before we send NotifyReq to driver.
    pending_inits: dashmap::DashMap<String, PendingInitInfo>,
    /// Per-instance serialization locks: all send paths acquire this before pushing
    /// CallReq/CallResult onto the runtime channel, ensuring strict FIFO ordering
    /// even when multiple tasks (flush_pending, dispatch_local_call, send_to_runtime)
    /// try to send to the same instance concurrently.
    instance_mailbox: InstanceMailbox,
}

impl BusProxyCoordinator {
    pub fn new(config: Arc<Config>, instance_ctrl: Arc<InstanceController>) -> Arc<Self> {
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
            instance_to_job: dashmap::DashMap::new(),
            result_to_caller: dashmap::DashMap::new(),
            request_to_message_id: dashmap::DashMap::new(),
            request_to_call: dashmap::DashMap::new(),
            request_to_sequence: dashmap::DashMap::new(),
            instance_next_sequence: dashmap::DashMap::new(),
            dispatched_request_to_instance: dashmap::DashMap::new(),
            notify_ack_to_runtime: dashmap::DashMap::new(),
            request_to_instance: dashmap::DashMap::new(),
            call_ack_to_caller: dashmap::DashMap::new(),
            state_snapshots: dashmap::DashMap::new(),
            active_instances: dashmap::DashMap::new(),
            pending_recovers: dashmap::DashMap::new(),
            cancelled_group_creates: dashmap::DashMap::new(),
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
                create_options: create.create_options.clone(),
                created_at: std::time::Instant::now(),
            },
        );
        self.instance_next_sequence
            .entry(instance_id.to_string())
            .or_insert(1);
        self.active_instances.insert(
            instance_id.to_string(),
            ActiveInstanceInfo {
                driver_stream_id: caller_stream_id.to_string(),
                function_name: create.function.clone(),
                trace_id: create.trace_id.clone(),
                create_options: create.create_options.clone(),
                create_args: create.args.clone(),
                remaining_recoveries: recover_retry_times(&create.create_options),
                recovering: false,
            },
        );
        self.instance_to_driver
            .insert(instance_id.to_string(), caller_stream_id.to_string());
    }

    pub fn attach_runtime_stream(
        &self,
        instance_id: &str,
        tx: mpsc::Sender<Result<StreamingMessage, tonic::Status>>,
    ) {
        self.runtime_tx.insert(instance_id.to_string(), tx);
        self.instance_view.mark_route_ready(instance_id);
    }

    pub fn detach_runtime_stream(&self, instance_id: &str) {
        self.runtime_tx.remove(instance_id);
        self.instance_view.remove_proxy(instance_id);
        self.instance_to_driver.remove(instance_id);
        self.instance_to_job.remove(instance_id);
        self.pending_inits.remove(instance_id);
        self.pending_creates.remove(instance_id);
        self.active_instances.remove(instance_id);
        self.pending_recovers.remove(instance_id);
        self.instance_next_sequence.remove(instance_id);
        self.instance_mailbox.remove(instance_id);
    }

    pub fn cancel_group_creates_for(&self, owner_or_group_id: &str) {
        if !owner_or_group_id.is_empty() {
            self.cancelled_group_creates
                .insert(owner_or_group_id.to_string(), ());
        }
    }

    pub fn clear_group_create_cancel(&self, owner_or_group_id: &str) {
        self.cancelled_group_creates.remove(owner_or_group_id);
    }

    pub fn is_group_create_cancelled(&self, owner_or_group_id: &str) -> bool {
        !owner_or_group_id.is_empty()
            && self.cancelled_group_creates.contains_key(owner_or_group_id)
    }

    /// Record the runtime id as soon as StartInstance succeeds.
    ///
    /// C++ can force-delete CREATING/SCHEDULING instances during group/range
    /// failure. Rust previously only filled runtime_id after runtime
    /// connect-back, so create-timeout cleanup could not stop already-forked
    /// runtimes that never finished init/connect-back.
    pub async fn mark_instance_started(
        &self,
        instance_id: &str,
        runtime_id: &str,
        runtime_port: i32,
    ) {
        let mut updated = None;
        if let Some(mut m) = self.instance_ctrl.instances().get_mut(instance_id) {
            m.runtime_id = runtime_id.to_string();
            m.runtime_port = runtime_port;
            let _ = m.transition(yr_common::types::InstanceState::Creating);
            updated = Some(m.clone());
        }
        if let Some(meta) = updated {
            self.instance_ctrl.persist_if_policy(&meta).await;
        }
    }

    /// Best-effort C++ ForceDeleteInstance equivalent for Rust-owned local instances.
    ///
    /// This is intentionally stronger than `detach_runtime_stream`: it stops the
    /// runtime process when a runtime_id is known, terminalizes local metadata, and
    /// then clears all proxy routing/mailbox maps. It is used by timeout/rollback
    /// paths where leaving a process alive poisons later ST cases.
    pub async fn force_cleanup_instance(&self, instance_id: &str, reason: &str) {
        // Intentional cleanup must not be mistaken for an abnormal runtime exit.
        // `handle_runtime_stream_closed` restarts instances while they remain in
        // `active_instances`, so remove recovery/create indices before sending
        // StopInstance. This mirrors C++ ForceDeleteInstance semantics: once a
        // range/group/timeout path decides to delete an instance, recovery is no
        // longer allowed to resurrect it.
        self.active_instances.remove(instance_id);
        self.pending_recovers.remove(instance_id);
        self.pending_creates.remove(instance_id);
        self.pending_inits.remove(instance_id);

        if let Some(m) = self.instance_ctrl.get(instance_id) {
            if !m.runtime_id.is_empty() {
                if let Err(e) = self
                    .instance_ctrl
                    .stop_instance(instance_id, &m.runtime_id, true)
                    .await
                {
                    warn!(
                        %instance_id,
                        runtime_id = %m.runtime_id,
                        %reason,
                        error = %e,
                        "force cleanup: stop_instance failed"
                    );
                }
            }
        }

        let mut updated = None;
        if let Some(mut m) = self.instance_ctrl.instances().get_mut(instance_id) {
            if m.transition(yr_common::types::InstanceState::Exiting)
                .is_err()
            {
                m.state = yr_common::types::InstanceState::Exiting;
                m.updated_at_ms = crate::state_machine::InstanceMetadata::now_ms();
            }
            updated = Some(m.clone());
        }
        if let Some(meta) = updated {
            self.instance_ctrl.persist_if_policy(&meta).await;
        }

        self.detach_runtime_stream(instance_id);
        info!(%instance_id, %reason, "force cleanup completed");
    }

    /// C++ `SHUT_DOWN_SIGNAL_GROUP` / `KillGroupInstance` equivalent.
    ///
    /// The C++ SDK may send signal=4 either with an explicit group id, or from
    /// inside a runtime with an empty/own instance id while terminating a range
    /// handle created by that runtime. In the latter case the children are the
    /// instances whose owner stream is the caller runtime; killing the caller
    /// itself is wrong and leaves the children alive.
    pub async fn execute_group_kill(
        &self,
        target_id: &str,
        caller_id: &str,
        signal: i32,
    ) -> (i32, String) {
        self.cancel_group_creates_for(target_id);
        self.cancel_group_creates_for(caller_id);
        let mut owned: Vec<String> = Vec::new();

        if target_id.starts_with("grp-") {
            owned.extend(
                self.instance_ctrl
                    .instances()
                    .iter()
                    .filter(|e| e.group_id.as_deref() == Some(target_id))
                    .map(|e| e.key().clone()),
            );
        } else {
            owned.extend(
                self.instance_to_driver
                    .iter()
                    .filter(|e| {
                        let owner = e.value().as_str();
                        owner == caller_id || owner == target_id
                    })
                    .map(|e| e.key().clone()),
            );
            owned.extend(
                self.instance_ctrl
                    .instances()
                    .iter()
                    .filter(|e| e.group_id.as_deref() == Some(target_id))
                    .map(|e| e.key().clone()),
            );
        }

        owned.retain(|id| id != target_id && id != caller_id);
        owned.sort();
        owned.dedup();

        let count = owned.len();
        for id in owned {
            self.force_cleanup_instance(&id, "group kill request").await;
        }
        info!(%target_id, %caller_id, signal, count, "execute_group_kill completed");
        (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
    }

    pub async fn handle_runtime_stream_closed(self: &Arc<Self>, instance_id: &str) {
        let runtime_id = self
            .instance_ctrl
            .get(instance_id)
            .map(|m| m.runtime_id)
            .unwrap_or_default();
        self.runtime_tx.remove(instance_id);
        self.pending_inits.remove(instance_id);
        self.pending_recovers.remove(instance_id);
        self.instance_mailbox.remove(instance_id);

        let Some(mut active) = self.active_instances.get_mut(instance_id) else {
            self.instance_view.remove_proxy(instance_id);
            self.instance_to_driver.remove(instance_id);
            self.instance_to_job.remove(instance_id);
            return;
        };

        if active.remaining_recoveries <= 0 {
            drop(active);
            self.fail_inflight_requests_for_instance(instance_id, &runtime_id)
                .await;
            self.detach_runtime_stream(instance_id);
            return;
        }

        active.remaining_recoveries -= 1;
        active.recovering = true;
        let launch = active.clone();
        drop(active);

        self.instance_view.ensure_proxy(instance_id);
        self.requeue_requests_for_recovery(instance_id);
        warn!(
            %instance_id,
            remaining_recoveries = launch.remaining_recoveries,
            "runtime stream closed; restarting instance for RecoverRetryTimes"
        );

        let bus = Arc::clone(self);
        let iid = instance_id.to_string();
        tokio::spawn(async move {
            let mut last_error = String::new();
            for attempt in 1..=20 {
                match bus
                    .schedule_instance_via_agent(
                        &iid,
                        &launch.function_name,
                        &launch.trace_id,
                        &launch.create_options,
                        &launch.create_args,
                    )
                    .await
                {
                    Ok((runtime_id, runtime_port)) => {
                        info!(instance_id = %iid, %runtime_id, runtime_port, attempt, "recover restart scheduled");
                        return;
                    }
                    Err(e) => {
                        last_error = e.to_string();
                        warn!(instance_id = %iid, attempt, error = %last_error, "recover restart scheduling failed; retrying");
                        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
                    }
                }
            }
            warn!(instance_id = %iid, error = %last_error, "recover restart retries exhausted");
            if let Some(mut a) = bus.active_instances.get_mut(&iid) {
                a.recovering = false;
            }
            let runtime_id = bus
                .instance_ctrl
                .get(&iid)
                .map(|m| m.runtime_id)
                .unwrap_or_default();
            bus.fail_inflight_requests_for_instance(&iid, &runtime_id)
                .await;
            bus.instance_view.remove_proxy(&iid);
        });
    }

    pub async fn cleanup_driver_stream(&self, driver_id: &str) {
        let owned: Vec<String> = self
            .instance_to_driver
            .iter()
            .filter(|e| e.value().as_str() == driver_id)
            .map(|e| e.key().clone())
            .collect();
        for id in owned {
            if let Some(m) = self.instance_ctrl.get(&id) {
                let _ = self
                    .instance_ctrl
                    .stop_instance(&id, &m.runtime_id, true)
                    .await;
            }
            self.detach_runtime_stream(&id);
        }
        self.detach_runtime_stream(driver_id);
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
            let ep = r.address.or(r.grpc).filter(|s| !s.is_empty());
            if let Some(ep) = ep {
                self.peer_by_node.write().insert(node_id.to_string(), ep);
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
        self.routes
            .write()
            .insert(instance_id.to_string(), rec.clone());
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

    fn resolve_known_instance_id(&self, instance_id: &str) -> Option<String> {
        if self.runtime_tx.contains_key(instance_id)
            || self.instance_ctrl.get(instance_id).is_some()
            || self.routes.read().contains_key(instance_id)
        {
            return Some(instance_id.to_string());
        }
        if instance_id.trim().is_empty() || instance_id.contains('-') {
            return None;
        }
        let suffix = format!("-{}", instance_id);
        let mut matches = Vec::new();
        for key in self.runtime_tx.iter().map(|e| e.key().clone()) {
            if key.ends_with(&suffix) {
                matches.push(key);
            }
        }
        for key in self
            .instance_ctrl
            .instances()
            .iter()
            .map(|e| e.key().clone())
        {
            if key.ends_with(&suffix) {
                matches.push(key);
            }
        }
        for key in self.routes.read().keys().cloned() {
            if key.ends_with(&suffix) {
                matches.push(key);
            }
        }
        matches.sort();
        matches.dedup();
        (matches.len() == 1).then(|| matches.remove(0))
    }

    fn get_instance_response_json(&self, instance_id: &str) -> Option<String> {
        let resolved = self.resolve_known_instance_id(instance_id)?;
        let meta = self.instance_ctrl.get(&resolved)?;
        if matches!(
            meta.state,
            yr_common::types::InstanceState::Exiting | yr_common::types::InstanceState::Exited
        ) {
            return None;
        }
        let active = self.active_instances.get(&resolved);
        let create_options = active
            .as_ref()
            .map(|a| a.create_options.clone())
            .unwrap_or_default();
        let create_args = active
            .as_ref()
            .map(|a| a.create_args.clone())
            .unwrap_or_default();
        let mut module_name = String::new();
        let mut function_name = meta.function_name.clone();
        let mut class_name = String::new();
        let mut language = 0i32;
        let mut function_id = String::new();
        let mut name = String::new();
        let mut ns = String::new();
        let mut is_async = false;
        let mut is_generator = false;
        if let Some(first) = create_args.first() {
            if let Ok(md) = yr_proto::resources::MetaData::decode(first.value.as_slice()) {
                if let Some(fm) = md.function_meta {
                    module_name = fm.module_name;
                    function_name = fm.function_name;
                    class_name = fm.class_name;
                    language = fm.language;
                    function_id = fm.function_id;
                    name = fm.name;
                    ns = fm.ns;
                    is_async = fm.is_async;
                    is_generator = fm.is_generator;
                }
            }
        }
        if name.is_empty() {
            if let Some((prefix, tail)) = resolved.rsplit_once('-') {
                if !tail.is_empty() {
                    name = tail.to_string();
                    ns = prefix.to_string();
                }
            }
        }
        let need_order = create_options.contains_key("need_order")
            || create_options.contains_key("NEED_ORDER")
            || create_options.contains_key("needOrder");
        Some(
            serde_json::json!({
                "applicationName": "",
                "moduleName": module_name,
                "functionName": function_name,
                "className": class_name,
                "language": language,
                "codeID": "",
                "signature": "",
                "apiType": 2,
                "name": name,
                "ns": ns,
                "functionID": function_id,
                "initializerCodeID": "",
                "isAsync": is_async,
                "isGenerator": is_generator,
                "needOrder": need_order,
            })
            .to_string(),
        )
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
        self.peer_by_node.read().get(&rec.owner_node_id).cloned()
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

    pub(crate) fn flush_pending(&self, instance_id: &str) {
        let Some(px) = self.instance_view.proxies().get(instance_id) else {
            return;
        };
        if self.is_pending_create(instance_id)
            || self.is_pending_init(instance_id)
            || self.is_recovering(instance_id)
        {
            return;
        }
        if !px.dispatcher.route_ready() {
            return;
        }
        let Some(tx_entry) = self.runtime_tx.get(instance_id) else {
            return;
        };
        let tx = tx_entry.clone();
        drop(tx_entry);
        let lock = self.mailbox(instance_id);
        let sequential = self.should_serialize_instance_invokes(instance_id);
        let dispatcher = px.dispatcher.clone();
        let dispatched = self.dispatched_request_to_instance.clone();
        let iid = instance_id.to_string();
        tokio::spawn(async move {
            let _guard = lock.lock().await;
            if sequential {
                if dispatched.iter().any(|e| e.value().as_str() == iid) {
                    return;
                }
                let Some(p) = dispatcher.pop_front() else {
                    return;
                };
                let Some(call) = p.req.req else {
                    return;
                };
                dispatched.insert(call.request_id.clone(), iid.clone());
                let msg = StreamingMessage {
                    message_id: uuid::Uuid::new_v4().to_string(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallReq(call)),
                };
                let _ = tx.send(Ok(msg)).await;
            } else {
                let pending = dispatcher.drain();
                if pending.is_empty() {
                    return;
                }
                for p in pending {
                    let Some(call) = p.req.req else { continue };
                    dispatched.insert(call.request_id.clone(), iid.clone());
                    let msg = StreamingMessage {
                        message_id: uuid::Uuid::new_v4().to_string(),
                        meta_data: Default::default(),
                        body: Some(streaming_message::Body::CallReq(call)),
                    };
                    let _ = tx.send(Ok(msg)).await;
                }
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
            .map(|rec| rec.owner_node_id == self.local_node_id || rec.owner_node_id.is_empty())
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
        px.dispatcher
            .enqueue(crate::busproxy::request_dispatcher::PendingForward {
                req: ForwardCallRequest {
                    req: Some(call),
                    instance_id: instance_id.to_string(),
                    src_ip: self.config.host.clone(),
                    src_node: self.local_node_id.clone(),
                },
                seq_no: None,
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

    pub async fn forward_kill(
        &self,
        req: ForwardKillRequest,
    ) -> Result<ForwardKillResponse, tonic::Status> {
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
            let state = self.load_state_snapshot(instance_id).unwrap_or_default();
            let create_options = self
                .active_instances
                .get(instance_id)
                .map(|a| a.create_options.clone())
                .unwrap_or_default();
            let recover_msg = StreamingMessage {
                message_id: format!("recover-{}", instance_id),
                meta_data: Default::default(),
                body: Some(streaming_message::Body::RecoverReq(
                    yr_proto::runtime_service::RecoverRequest {
                        state,
                        create_options,
                    },
                )),
            };
            self.pending_recovers.insert(instance_id.to_string(), ());
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
                self.runtime_tx
                    .iter()
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
            if let Some(tx) = self.runtime_tx.get(instance_id) {
                let ack = StreamingMessage {
                    message_id: res.request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallResultAck(CallResultAck {
                        code: yr_proto::common::ErrorCode::ErrNone as i32,
                        message: String::new(),
                    })),
                };
                let _ = tx.send(Ok(ack)).await;
            }
            return;
        }

        // Normal CallResult: route back to the stream that issued the InvokeReq.
        // For driver-origin calls this is the driver stream; for nested actor calls
        // it is the source runtime stream. Falling back to instance_to_driver keeps
        // older one-hop calls working when a request was not recorded.
        let pending_request = self.request_to_instance.contains_key(&res.request_id)
            || self.request_to_call.contains_key(&res.request_id);
        let target_id = self
            .result_to_caller
            .remove(&res.request_id)
            .map(|(_, v)| v)
            .or_else(|| {
                if !pending_request {
                    return None;
                }
                self.instance_to_driver
                    .get(instance_id)
                    .map(|e| e.value().clone())
            });

        let outer_message_id = self
            .request_to_message_id
            .remove(&res.request_id)
            .map(|(_, v)| v);

        if let (Some(did), Some(outer_message_id)) = (target_id, outer_message_id) {
            if let Some(tx) = self.runtime_tx.get(&did) {
                let fwd = StreamingMessage {
                    message_id: outer_message_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CallResultReq(res.clone())),
                };
                let _ = tx.send(Ok(fwd)).await;
                self.notify_ack_to_runtime
                    .insert(outer_message_id.clone(), instance_id.to_string());
                info!(%instance_id, target = %did, request_id = %res.request_id, "forwarded CallResult to requester");
            }
        } else {
            warn!(
                %instance_id,
                request_id = %res.request_id,
                "CallResult: no pending caller/message mapping, dropping"
            );
        }

        self.request_to_instance.remove(&res.request_id);
        self.request_to_call.remove(&res.request_id);
        self.dispatched_request_to_instance.remove(&res.request_id);
        self.request_to_sequence.remove(&res.request_id);

        debug!(%instance_id, request_id = %res.request_id, "runtime CallResult handled");
    }

    pub async fn notify_inner(
        &self,
        instance_id: &str,
        n: &yr_proto::runtime_service::NotifyRequest,
    ) -> Result<(), tonic::Status> {
        let _ = self.posix.lock().await.notify_result(instance_id, n).await;
        Ok(())
    }

    fn enrich_create_metadata_code_path(
        &self,
        function_name: &str,
        args: &mut [yr_proto::common::Arg],
    ) {
        let Some(code_path) = self.instance_ctrl.service_code_path_for(function_name) else {
            return;
        };
        let Some(first) = args.first_mut() else {
            return;
        };
        let Ok(mut meta) = yr_proto::resources::MetaData::decode(first.value.as_slice()) else {
            return;
        };
        let cfg = meta.config.get_or_insert_with(Default::default);
        if cfg.code_paths.is_empty() {
            cfg.code_paths.push(code_path);
            let mut buf = Vec::new();
            if meta.encode(&mut buf).is_ok() {
                first.value = buf;
            }
        }
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

        let pending_info = self.pending_creates.remove(instance_id).map(|(_, v)| v);

        if pending_info.is_none() {
            if let Some(active) = self
                .active_instances
                .get(instance_id)
                .filter(|a| a.recovering)
                .map(|a| a.clone())
            {
                let state = self.load_state_snapshot(instance_id).unwrap_or_default();
                let recover_msg = StreamingMessage {
                    message_id: format!("recover-{}", instance_id),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::RecoverReq(
                        yr_proto::runtime_service::RecoverRequest {
                            state,
                            create_options: active.create_options.clone(),
                        },
                    )),
                };
                self.pending_recovers.insert(instance_id.to_string(), ());
                if let Some(tx) = self.runtime_tx.get(instance_id) {
                    let lock = self.mailbox(instance_id);
                    let _guard = lock.lock().await;
                    let _ = tx.send(Ok(recover_msg)).await;
                    info!(%instance_id, "sent RecoverReq to restarted runtime");
                }
                return;
            }

            warn!(%instance_id, "no pending create found for runtime connect-back");
            return;
        }

        let pending_info = pending_info.expect("checked is_some above");

        // Mark init-pending before the create ack can release the caller. Group
        // creates can be followed immediately by invokes; those must be buffered
        // until the runtime has completed its isCreate=true initialization call.
        self.pending_inits.insert(
            instance_id.to_string(),
            PendingInitInfo {
                driver_stream_id: pending_info.driver_stream_id.clone(),
                create_request_id: pending_info.create_request_id.clone(),
            },
        );

        // Give the CreateReq handler a chance to flush CreateRsp to the driver before
        // the runtime init NotifyReq/CallResult path starts. C++ sends create ack before
        // later readiness notification; the C++ SDK is order-sensitive here.
        tokio::time::sleep(std::time::Duration::from_millis(30)).await;

        // Step 1: Send isCreate=true CallRequest to the runtime to trigger init handler.
        // The C++ libruntime requires this before processing any isCreate=false calls.
        // If the driver provided args in CreateReq (stateful actor with MetaData containing
        // InvokeType::CreateInstance), forward those args directly so the Python handler's
        // __create_instance is called. Otherwise, use a default minimal MetaData with
        // InvokeType::CreateInstanceStateless for stateless functions.
        let init_args = if pending_info.create_args.is_empty() {
            let mut meta = yr_proto::resources::MetaData {
                invoke_type: 2, // InvokeType::CreateInstanceStateless
                function_meta: None,
                config: Some(yr_proto::resources::MetaConfig::default()),
            };
            if let Some(code_path) = self
                .instance_ctrl
                .service_code_path_for(&pending_info.function_name)
            {
                if let Some(cfg) = meta.config.as_mut() {
                    cfg.code_paths.push(code_path);
                }
            }
            let mut metadata_bytes = Vec::new();
            let _ = meta.encode(&mut metadata_bytes);
            vec![yr_proto::common::Arg {
                value: metadata_bytes,
                ..Default::default()
            }]
        } else {
            let mut args = pending_info.create_args.clone();
            self.enrich_create_metadata_code_path(&pending_info.function_name, &mut args);
            info!(
                %instance_id,
                args_count = args.len(),
                first_arg_bytes = ?args.first().map(|a| &a.value[..]),
                "using driver-provided CreateReq args for init CallReq (stateful actor)"
            );
            args
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
        trace_id: &str,
        create_options: &std::collections::HashMap<String, String>,
        create_args: &[yr_proto::common::Arg],
    ) -> Result<(String, i32), tonic::Status> {
        let resources = self.instance_ctrl.clamp_resources(&Default::default());
        self.instance_ctrl
            .start_instance(
                instance_id,
                function_name,
                "",
                resources,
                "default",
                create_options,
                trace_id,
                create_args,
            )
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
                    bus.force_cleanup_instance(&iid, "pending create connect-back timeout")
                        .await;
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
        if instance_id.starts_with("grp-") {
            self.cancel_group_creates_for(instance_id);
            let owned: Vec<String> = self
                .instance_ctrl
                .instances()
                .iter()
                .filter(|e| e.group_id.as_deref() == Some(instance_id))
                .map(|e| e.key().clone())
                .collect();
            let count = owned.len();
            for id in owned {
                self.force_cleanup_instance(&id, "group kill request").await;
            }
            info!(group_id = %instance_id, signal, count, "execute_kill: group cleanup completed");
            return (yr_proto::common::ErrorCode::ErrNone as i32, String::new());
        }

        if instance_id.starts_with("job-") {
            let driver_id = format!("driver-{instance_id}");
            let mut owned: Vec<String> = self
                .instance_to_job
                .iter()
                .filter(|e| e.value().as_str() == instance_id)
                .map(|e| e.key().clone())
                .collect();
            owned.extend(
                self.instance_to_driver
                    .iter()
                    .filter(|e| e.value().as_str() == driver_id)
                    .map(|e| e.key().clone()),
            );
            owned.sort();
            owned.dedup();
            let count = owned.len();
            for id in owned {
                self.force_cleanup_instance(&id, "job kill request").await;
            }
            info!(job_id = %instance_id, signal, count, "execute_kill: job cleanup completed");
            return (yr_proto::common::ErrorCode::ErrNone as i32, String::new());
        }
        if self.should_dispatch_locally(instance_id) {
            let force = !matches!(signal, 1 | 3);
            if !force && self.has_runtime_stream(instance_id) {
                let grace_period_second = self
                    .active_instances
                    .get(instance_id)
                    .map(|a| graceful_shutdown_seconds(&a.create_options))
                    .unwrap_or(60);
                self.request_runtime_shutdown(instance_id, grace_period_second)
                    .await;
                let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
                while self.has_runtime_stream(instance_id) && std::time::Instant::now() < deadline {
                    tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                }
            }

            self.force_cleanup_instance(instance_id, "instance kill request")
                .await;

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
                    (
                        yr_proto::common::ErrorCode::ErrInnerSystemError as i32,
                        e.message().to_string(),
                    )
                }
            }
        } else {
            warn!(
                target = %target_id,
                caller = %caller_id,
                signal = kill.signal,
                "forward_user_signal: no stream for local target"
            );
            (
                yr_proto::common::ErrorCode::ErrInstanceNotFound as i32,
                format!("no runtime stream for instance {target_id}"),
            )
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

    pub fn is_pending_init(&self, instance_id: &str) -> bool {
        self.pending_inits.contains_key(instance_id)
    }

    pub fn is_pending_create(&self, instance_id: &str) -> bool {
        self.pending_creates.contains_key(instance_id)
    }

    pub fn is_recovering(&self, instance_id: &str) -> bool {
        self.active_instances
            .get(instance_id)
            .map(|a| a.recovering)
            .unwrap_or(false)
            || self.pending_recovers.contains_key(instance_id)
    }

    pub fn on_runtime_recover_response(
        &self,
        instance_id: &str,
        rsp: &yr_proto::runtime_service::RecoverResponse,
    ) {
        self.pending_recovers.remove(instance_id);
        if rsp.code == yr_proto::common::ErrorCode::ErrNone as i32 {
            if let Some(mut active) = self.active_instances.get_mut(instance_id) {
                active.recovering = false;
            }
            if let Some(mut m) = self.instance_ctrl.instances().get_mut(instance_id) {
                let _ = m.transition(yr_common::types::InstanceState::Running);
            }
            info!(%instance_id, "RecoverRsp success; flushing queued invokes");
            self.flush_pending(instance_id);
        } else {
            warn!(
                %instance_id,
                code = rsp.code,
                message = %rsp.message,
                "RecoverRsp failed"
            );
            self.instance_view.remove_proxy(instance_id);
        }
    }

    pub fn remember_result_target(&self, request_id: &str, caller_stream_id: &str) {
        if request_id.trim().is_empty() || caller_stream_id.trim().is_empty() {
            return;
        }
        self.result_to_caller
            .insert(request_id.to_string(), caller_stream_id.to_string());
    }

    pub fn remember_request_target(&self, request_id: &str, target_instance: &str) {
        if request_id.trim().is_empty() || target_instance.trim().is_empty() {
            return;
        }
        self.request_to_instance
            .insert(request_id.to_string(), target_instance.to_string());
    }

    pub fn remember_request_sequence(&self, request_id: &str, seq_no: i64) {
        if request_id.trim().is_empty() || seq_no <= 0 {
            return;
        }
        self.request_to_sequence
            .insert(request_id.to_string(), seq_no);
    }

    pub fn expected_sequence_for_instance(&self, instance_id: &str) -> i64 {
        self.instance_next_sequence
            .get(instance_id)
            .map(|v| *v)
            .unwrap_or(1)
    }

    pub fn has_inflight_request_for_instance(&self, instance_id: &str) -> bool {
        self.request_to_instance
            .iter()
            .any(|e| e.value().as_str() == instance_id)
    }

    pub fn has_other_inflight_request_for_instance(
        &self,
        instance_id: &str,
        request_id: &str,
    ) -> bool {
        self.dispatched_request_to_instance
            .iter()
            .any(|e| e.key().as_str() != request_id && e.value().as_str() == instance_id)
    }

    pub fn remember_dispatched_request(&self, request_id: &str, instance_id: &str) {
        if request_id.trim().is_empty() || instance_id.trim().is_empty() {
            return;
        }
        self.dispatched_request_to_instance
            .insert(request_id.to_string(), instance_id.to_string());
    }

    pub fn should_serialize_instance_invokes(&self, instance_id: &str) -> bool {
        let Some(active) = self.active_instances.get(instance_id) else {
            return false;
        };
        let opts = &active.create_options;
        let concurrency = opts
            .get("Concurrency")
            .or_else(|| opts.get("CONCURRENCY"))
            .and_then(|v| v.parse::<i32>().ok())
            .unwrap_or(1);
        concurrency <= 1
    }

    pub fn remember_request_message_id(&self, request_id: &str, message_id: &str) {
        if request_id.trim().is_empty() || message_id.trim().is_empty() {
            return;
        }
        self.request_to_message_id
            .insert(request_id.to_string(), message_id.to_string());
    }

    pub fn remember_request_call(&self, request_id: &str, call: &CallRequest) {
        if request_id.trim().is_empty() {
            return;
        }
        self.request_to_call
            .insert(request_id.to_string(), call.clone());
    }

    pub fn remember_call_ack_target(&self, message_id: &str, caller_stream_id: &str) {
        if message_id.trim().is_empty() || caller_stream_id.trim().is_empty() {
            return;
        }
        self.call_ack_to_caller
            .insert(message_id.to_string(), caller_stream_id.to_string());
    }

    pub async fn on_runtime_call_ack(
        &self,
        instance_id: &str,
        message_id: &str,
        rsp: yr_proto::runtime_service::CallResponse,
    ) {
        let Some((_, caller_id)) = self.call_ack_to_caller.remove(message_id) else {
            debug!(%instance_id, %message_id, "CallRsp: no caller mapping, dropping");
            return;
        };
        let forwarded = StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallRsp(rsp)),
        };
        if let Some(tx) = self.runtime_tx.get(&caller_id) {
            let _ = tx.send(Ok(forwarded)).await;
            debug!(%instance_id, caller = %caller_id, %message_id, "forwarded CallRsp to requester");
        } else {
            warn!(%instance_id, caller = %caller_id, %message_id, "CallRsp: caller stream missing");
        }
    }

    pub fn save_state_snapshot(&self, instance_id: &str, state: Vec<u8>) {
        self.state_snapshots.insert(instance_id.to_string(), state);
    }

    pub fn load_state_snapshot(&self, checkpoint_id: &str) -> Option<Vec<u8>> {
        self.state_snapshots
            .get(checkpoint_id)
            .map(|v| v.value().clone())
    }

    pub async fn forward_notify_rsp_ack(&self, message_id: &str) {
        let Some((_, runtime_instance)) = self.notify_ack_to_runtime.remove(message_id) else {
            return;
        };
        let Some(tx) = self.runtime_tx.get(&runtime_instance) else {
            return;
        };
        let ack = StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallResultAck(CallResultAck {
                code: yr_proto::common::ErrorCode::ErrNone as i32,
                message: String::new(),
            })),
        };
        let _ = tx.send(Ok(ack)).await;
    }

    fn detect_runtime_signal(&self, runtime_id: &str) -> Option<String> {
        if runtime_id.trim().is_empty() {
            return None;
        }
        let deploy_root = std::path::Path::new("/tmp/deploy");
        let signals = [
            "SIGFPE", "SIGSEGV", "SIGILL", "SIGABRT", "SIGINT", "SIGTERM",
        ];
        let dirs = fs::read_dir(deploy_root).ok()?;
        for dir in dirs.flatten() {
            let log_dir = dir.path().join("log");
            if !log_dir.is_dir() {
                continue;
            }
            let entries = match fs::read_dir(&log_dir) {
                Ok(x) => x,
                Err(_) => continue,
            };
            for entry in entries.flatten() {
                let path = entry.path();
                let Some(name) = path.file_name().and_then(|s| s.to_str()) else {
                    continue;
                };
                if !name.starts_with(runtime_id) || !name.ends_with(".stderr.log") {
                    continue;
                }
                let text = match fs::read_to_string(&path) {
                    Ok(t) => t,
                    Err(_) => continue,
                };
                for sig in signals {
                    if text.contains(sig) {
                        return Some(sig.to_string());
                    }
                }
            }
        }
        None
    }

    async fn fail_inflight_requests_for_instance(&self, instance_id: &str, runtime_id: &str) {
        let signal = self.detect_runtime_signal(runtime_id);
        let message = signal
            .clone()
            .unwrap_or_else(|| "instance occurs fatal error".to_string());
        let code = yr_proto::common::ErrorCode::ErrUserFunctionException as i32;
        let request_ids: Vec<String> = self
            .request_to_instance
            .iter()
            .filter(|e| e.value().as_str() == instance_id)
            .map(|e| e.key().clone())
            .collect();
        for request_id in request_ids {
            self.request_to_instance.remove(&request_id);
            self.request_to_call.remove(&request_id);
            let outer_message_id = self
                .request_to_message_id
                .remove(&request_id)
                .map(|(_, v)| v)
                .unwrap_or_else(|| request_id.clone());
            let Some((_, caller_id)) = self.result_to_caller.remove(&request_id) else {
                continue;
            };
            let Some(tx) = self.runtime_tx.get(&caller_id) else {
                continue;
            };
            let fwd = StreamingMessage {
                message_id: outer_message_id,
                meta_data: Default::default(),
                body: Some(streaming_message::Body::CallResultReq(CallResult {
                    request_id: request_id.clone(),
                    code,
                    message: message.clone(),
                    small_objects: Vec::new(),
                    stack_trace_infos: Vec::new(),
                    runtime_info: None,
                    instance_id: instance_id.to_string(),
                })),
            };
            let _ = tx.send(Ok(fwd)).await;
            warn!(%instance_id, %caller_id, %request_id, error = %message, "failed inflight request after runtime stream close");
        }
    }

    fn requeue_requests_for_recovery(&self, instance_id: &str) {
        let Some(px) = self.instance_view.proxies().get(instance_id) else {
            return;
        };
        let existing = px.dispatcher.drain();
        let mut existing_request_ids = std::collections::HashSet::new();
        for pending in existing {
            if let Some(call) = pending.req.req.as_ref() {
                existing_request_ids.insert(call.request_id.clone());
            }
            px.dispatcher.enqueue(pending);
        }

        let dispatched_ids: Vec<String> = self
            .dispatched_request_to_instance
            .iter()
            .filter(|e| e.value().as_str() == instance_id)
            .map(|e| e.key().clone())
            .collect();
        for request_id in dispatched_ids {
            self.dispatched_request_to_instance.remove(&request_id);
        }

        let request_ids: Vec<String> = self
            .request_to_instance
            .iter()
            .filter(|e| e.value().as_str() == instance_id)
            .map(|e| e.key().clone())
            .collect();
        for request_id in request_ids {
            if existing_request_ids.contains(&request_id) {
                continue;
            }
            let Some(call) = self.request_to_call.get(&request_id).map(|v| v.clone()) else {
                continue;
            };
            let seq_no = self.request_to_sequence.get(&request_id).map(|v| *v);
            px.dispatcher
                .enqueue(crate::busproxy::request_dispatcher::PendingForward {
                    req: ForwardCallRequest {
                        req: Some(call),
                        instance_id: instance_id.to_string(),
                        src_ip: String::new(),
                        src_node: String::new(),
                    },
                    seq_no,
                });
        }
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

    pub async fn request_runtime_shutdown(&self, instance_id: &str, grace_period_second: u64) {
        self.send_to_runtime(
            instance_id,
            StreamingMessage {
                message_id: uuid::Uuid::new_v4().to_string(),
                meta_data: Default::default(),
                body: Some(streaming_message::Body::ShutdownReq(
                    yr_proto::runtime_service::ShutdownRequest {
                        grace_period_second,
                    },
                )),
            },
        )
        .await;
    }

    pub async fn handle_runtime_message(
        &self,
        instance_id: &str,
        msg: StreamingMessage,
    ) -> invocation_handler::InboundAction {
        InvocationHandler::handle_runtime_inbound(instance_id, msg, self).await
    }
}
