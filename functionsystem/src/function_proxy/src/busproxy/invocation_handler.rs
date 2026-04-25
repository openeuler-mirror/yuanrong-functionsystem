//! Maps runtime `StreamingMessage` variants to the call / result plane.

use prost::Message;
use tracing::{debug, info, warn};
use yr_proto::common::ErrorCode;
use yr_proto::core_service as cs;
use yr_proto::inner_service::ForwardCallRequest;
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service as rs;

use super::BusProxyCoordinator;

pub enum InboundAction {
    /// Messages to send back on the runtime stream (same instance).
    Reply(Vec<StreamingMessage>),
    None,
}

pub struct InvocationHandler;

#[derive(Clone, PartialEq, ::prost::Message)]
struct InvocationMetaLite {
    #[prost(string, tag = "1")]
    pub invoker_runtime_id: String,
    #[prost(int64, tag = "2")]
    pub invocation_sequence_no: i64,
    #[prost(int64, tag = "3")]
    pub min_unfinished_sequence_no: i64,
}

#[derive(Clone, PartialEq, ::prost::Message)]
struct MetaDataLite {
    #[prost(int32, tag = "1")]
    pub invoke_type: i32,
    #[prost(message, optional, tag = "4")]
    pub invocation_meta: Option<InvocationMetaLite>,
}

impl InvocationHandler {
    fn accepted_call_rsp(message_id: &str) -> StreamingMessage {
        StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallRsp(rs::CallResponse {
                code: ErrorCode::ErrNone as i32,
                message: String::new(),
            })),
        }
    }

    async fn handle_save_req(
        message_id: &str,
        instance_id: &str,
        save: &cs::StateSaveRequest,
        bus: &BusProxyCoordinator,
    ) -> InboundAction {
        let checkpoint_id = instance_id.to_string();
        bus.save_state_snapshot(&checkpoint_id, save.state.clone());
        InboundAction::Reply(vec![StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::SaveRsp(cs::StateSaveResponse {
                code: ErrorCode::ErrNone as i32,
                message: String::new(),
                checkpoint_id,
            })),
        }])
    }

    async fn handle_load_req(
        message_id: &str,
        load: &cs::StateLoadRequest,
        bus: &BusProxyCoordinator,
    ) -> InboundAction {
        let (code, state, message) = match bus.load_state_snapshot(&load.checkpoint_id) {
            Some(state) => (ErrorCode::ErrNone as i32, state, String::new()),
            None => (
                ErrorCode::ErrInnerSystemError as i32,
                Vec::new(),
                format!(
                    "load state failed: checkpoint {} not found",
                    load.checkpoint_id
                ),
            ),
        };
        InboundAction::Reply(vec![StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::LoadRsp(cs::StateLoadResponse {
                code,
                message,
                state,
            })),
        }])
    }

    fn invoke_function_name(inv: &cs::InvokeRequest) -> Option<String> {
        let first = inv.args.first()?;
        let meta = yr_proto::resources::MetaData::decode(first.value.as_slice()).ok()?;
        let f = meta.function_meta?;
        (!f.function_name.trim().is_empty()).then_some(f.function_name)
    }

    fn invoke_sequence_no(inv: &cs::InvokeRequest) -> Option<i64> {
        let first = inv.args.first()?;
        let meta = MetaDataLite::decode(first.value.as_slice()).ok()?;
        let seq = meta.invocation_meta?.invocation_sequence_no;
        (seq > 0).then_some(seq)
    }

    /// Turn a core `InvokeRequest` into a runtime `CallRequest` wrapped as `StreamingMessage`.
    pub fn invoke_to_call(
        invoke: &cs::InvokeRequest,
        message_id: &str,
        sender_id: &str,
    ) -> StreamingMessage {
        let call = rs::CallRequest {
            function: invoke.function.clone(),
            args: invoke.args.clone(),
            trace_id: invoke.trace_id.clone(),
            request_id: invoke.request_id.clone(),
            return_object_id: message_id.to_string(),
            sender_id: sender_id.to_string(),
            return_object_i_ds: invoke.return_object_i_ds.clone(),
            span_id: invoke.span_id.clone(),
            ..Default::default()
        };
        StreamingMessage {
            message_id: message_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallReq(call)),
        }
    }

    /// Handle a CreateReq from the driver: trigger real scheduling via agent StartInstance.
    /// The runtime process is forked by yr-agent's embedded runtime_manager and will
    /// connect back to the proxy's POSIX gRPC port via MessageStream.
    async fn handle_create_req(
        msg_id: &str,
        create: &cs::CreateRequest,
        bus: &BusProxyCoordinator,
        caller_stream_id: &str,
    ) -> InboundAction {
        let instance_id = if create.designated_instance_id.is_empty() {
            // C++ local scheduler uses the create request id as the implicit
            // instance id. The C++ SDK may issue InvokeReqs against that id
            // before all create notifications settle, so a random id breaks
            // pending-create correlation and strands later invokes.
            create.request_id.clone()
        } else {
            create.designated_instance_id.clone()
        };

        info!(
            %instance_id,
            function = %create.function,
            request_id = %create.request_id,
            caller = %caller_stream_id,
            args_count = create.args.len(),
            args_bytes = ?create.args.iter().map(|a| a.value.len()).collect::<Vec<_>>(),
            "CreateReq: scheduling instance via agent"
        );

        if bus.pending_creates.contains_key(&instance_id) || bus.has_runtime_stream(&instance_id) {
            info!(
                %instance_id,
                request_id = %create.request_id,
                "CreateReq: duplicate create for existing/pending instance, returning idempotent success"
            );
            return InboundAction::Reply(vec![
                StreamingMessage {
                    message_id: msg_id.to_string(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CreateRsp(cs::CreateResponse {
                        code: ErrorCode::ErrNone as i32,
                        message: "instance already scheduled".to_string(),
                        instance_id: instance_id.clone(),
                    })),
                },
                StreamingMessage {
                    message_id: create.request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::NotifyReq(rs::NotifyRequest {
                        request_id: create.request_id.clone(),
                        code: ErrorCode::ErrNone as i32,
                        message: String::new(),
                        small_objects: Vec::new(),
                        stack_trace_infos: Vec::new(),
                        runtime_info: None,
                    })),
                },
            ]);
        }

        bus.register_pending_instance(&instance_id, caller_stream_id, create);

        let start_result = bus
            .schedule_instance_via_agent(
                &instance_id,
                &create.function,
                &create.trace_id,
                &create.create_options,
                &create.args,
            )
            .await;

        match start_result {
            Ok((runtime_id, runtime_port)) => {
                bus.mark_instance_started(&instance_id, &runtime_id, runtime_port)
                    .await;
                info!(
                    %instance_id,
                    %runtime_id,
                    %runtime_port,
                    "CreateReq: agent StartInstance succeeded, waiting for runtime connect-back"
                );

                let create_rsp = StreamingMessage {
                    message_id: msg_id.to_string(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CreateRsp(cs::CreateResponse {
                        code: ErrorCode::ErrNone as i32,
                        message: format!("instance scheduled, runtime_id={runtime_id}"),
                        instance_id: instance_id.clone(),
                    })),
                };

                InboundAction::Reply(vec![create_rsp])
            }
            Err(e) => {
                warn!(
                    %instance_id,
                    error = %e,
                    "CreateReq: agent StartInstance failed"
                );

                let create_rsp = StreamingMessage {
                    message_id: msg_id.to_string(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::CreateRsp(cs::CreateResponse {
                        code: ErrorCode::ErrInnerSystemError as i32,
                        message: format!("scheduling failed: {e}"),
                        instance_id: instance_id.clone(),
                    })),
                };

                let notify = StreamingMessage {
                    message_id: create.request_id.clone(),
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::NotifyReq(rs::NotifyRequest {
                        request_id: create.request_id.clone(),
                        code: ErrorCode::ErrInnerSystemError as i32,
                        message: format!("scheduling failed: {e}"),
                        small_objects: Vec::new(),
                        stack_trace_infos: Vec::new(),
                        runtime_info: None,
                    })),
                };

                InboundAction::Reply(vec![create_rsp, notify])
            }
        }
    }

    /// Handle KillReq from the stream.
    ///
    /// C++ behavior (InstanceCtrlActor::HandleKill) dispatches on signal value:
    ///   1  SHUT_DOWN_SIGNAL            – kill instance
    ///   2  SHUT_DOWN_SIGNAL_ALL        – kill all instances of a job
    ///   3  SHUT_DOWN_SIGNAL_SYNC       – kill instance synchronously
    ///   4  SHUT_DOWN_SIGNAL_GROUP      – kill group
    ///   5  GROUP_EXIT_SIGNAL           – set instance fatal
    ///   6  FAMILY_EXIT_SIGNAL          – set instance fatal
    ///   7  APP_STOP_SIGNAL             – stop app driver
    ///   8  REMOVE_RESOURCE_GROUP       – kill resource group
    ///   9  SUBSCRIBE_SIGNAL            – subscription management (NOT a kill!)
    ///  10  NOTIFY_SIGNAL               – notification (NOT a kill!)
    ///  11  UNSUBSCRIBE_SIGNAL          – unsubscription (NOT a kill!)
    ///  12  INSTANCE_CHECKPOINT_SIGNAL  – make checkpoint
    ///  13  INSTANCE_TRANS_SUSPEND_SIGNAL – suspend after checkpoint
    ///  14-17  suspend/resume signals
    ///  64+ user-defined signals        – forwarded to target instance
    async fn handle_kill_req(
        msg_id: &str,
        caller_instance_id: &str,
        kill: &cs::KillRequest,
        bus: &BusProxyCoordinator,
    ) -> InboundAction {
        let target_id = if kill.instance_id.is_empty() {
            caller_instance_id.to_string()
        } else {
            kill.instance_id.clone()
        };

        let signal = kill.signal;

        // Signals 9-11 are subscription/notification management, NOT actual kills.
        // In C++ these route to SubscriptionMgr and never stop a runtime process.
        const SUBSCRIBE_SIGNAL: i32 = 9;
        const NOTIFY_SIGNAL: i32 = 10;
        const UNSUBSCRIBE_SIGNAL: i32 = 11;
        // Signals 12-17 are checkpoint / suspend / resume — not kills either.
        const INSTANCE_CHECKPOINT_SIGNAL: i32 = 12;
        const INSTANCE_TRANS_SUSPEND_SIGNAL: i32 = 13;
        const _INSTANCE_SUSPEND_SIGNAL: i32 = 14;
        const _INSTANCE_RESUME_SIGNAL: i32 = 15;
        const _GROUP_SUSPEND_SIGNAL: i32 = 16;
        const GROUP_RESUME_SIGNAL: i32 = 17;
        const MIN_USER_SIGNAL: i32 = 64;

        let (code, message, payload) = match signal {
            // Group shutdown is not a local instance kill. It recycles the
            // instances in a group/range created by this caller.
            4 => {
                info!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: executing group kill"
                );
                let (code, message) = bus
                    .execute_group_kill(&target_id, caller_instance_id, signal)
                    .await;
                (code, message, Vec::new())
            }
            // True instance/job kill signals: actually stop runtime processes.
            1 | 2 | 3 | 5 | 6 | 7 | 8 => {
                info!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: executing kill"
                );
                let (code, message) = bus.execute_kill(&target_id, signal).await;
                (code, message, Vec::new())
            }
            // Subscription/notification signals: ack without killing.
            SUBSCRIBE_SIGNAL | NOTIFY_SIGNAL | UNSUBSCRIBE_SIGNAL => {
                debug!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: subscription/notify signal, acking without kill"
                );
                (
                    yr_proto::common::ErrorCode::ErrNone as i32,
                    String::new(),
                    Vec::new(),
                )
            }
            // Checkpoint / suspend / resume: ack without killing.
            INSTANCE_CHECKPOINT_SIGNAL..=GROUP_RESUME_SIGNAL => {
                debug!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: checkpoint/suspend/resume signal, acking without kill"
                );
                (
                    yr_proto::common::ErrorCode::ErrNone as i32,
                    String::new(),
                    Vec::new(),
                )
            }
            // Query DS address.
            70 => (
                yr_proto::common::ErrorCode::ErrNone as i32,
                format!(
                    "{}:{}",
                    bus.config.data_system_host, bus.config.data_system_port
                ),
                Vec::new(),
            ),
            // GetInstance: return function meta JSON in KillResponse.message.
            74 => match bus.get_instance_response_json(&target_id) {
                Some(meta_json) => (
                    yr_proto::common::ErrorCode::ErrNone as i32,
                    meta_json,
                    Vec::new(),
                ),
                None => (
                    yr_proto::common::ErrorCode::ErrInstanceNotFound as i32,
                    format!("instance {} is not running", target_id),
                    Vec::new(),
                ),
            },
            // User-defined signals (≥64): forward to target runtime via its stream.
            s if s >= MIN_USER_SIGNAL => {
                info!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal = s,
                    "KillReq: user signal, forwarding to target"
                );
                let (code, message) = bus
                    .forward_user_signal(&target_id, caller_instance_id, kill)
                    .await;
                (code, message, Vec::new())
            }
            _ => {
                warn!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: unexpected signal value"
                );
                (
                    yr_proto::common::ErrorCode::ErrNone as i32,
                    String::new(),
                    Vec::new(),
                )
            }
        };

        let rsp = cs::KillResponse {
            code,
            message,
            payload,
        };
        InboundAction::Reply(vec![StreamingMessage {
            message_id: msg_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::KillRsp(rsp)),
        }])
    }

    /// Handle ExitReq from the stream.
    ///
    /// C++ behavior (InstanceCtrlActor::HandleExit):
    /// - If exit code is ERR_NONE (0): converts to Kill(signal=1) fire-and-forget
    /// - Non-zero: treated as abnormal exit
    async fn handle_exit_req(
        msg_id: &str,
        caller_instance_id: &str,
        exit: &cs::ExitRequest,
        bus: &BusProxyCoordinator,
    ) -> InboundAction {
        info!(
            caller = %caller_instance_id,
            exit_code = exit.code,
            "ExitReq: processing exit with side effects"
        );

        if exit.code == 0 {
            let _ = bus.execute_kill(caller_instance_id, 1).await;
        } else {
            bus.apply_instance_exit(caller_instance_id, false, &exit.message)
                .await;
        }

        let rsp = cs::ExitResponse {
            code: ErrorCode::ErrNone as i32,
            message: String::new(),
        };
        InboundAction::Reply(vec![StreamingMessage {
            message_id: msg_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::ExitRsp(rsp)),
        }])
    }

    /// Handle GroupCreate (createReqs): batch-schedule N instances.
    ///
    /// C++: PosixAPIHandler::GroupCreate → localGroupCtrl->GroupSchedule
    async fn handle_group_create(
        msg_id: &str,
        creates: &cs::CreateRequests,
        bus: &BusProxyCoordinator,
        caller_stream_id: &str,
    ) -> InboundAction {
        let group_id = format!("grp-{:016x}", uuid::Uuid::new_v4().as_u128());
        let mut instance_ids = Vec::with_capacity(creates.requests.len());
        let mut failed = false;
        let mut cancelled = false;

        info!(
            %group_id,
            request_id = %creates.request_id,
            count = creates.requests.len(),
            "GroupCreate: batch scheduling"
        );

        for create in &creates.requests {
            let range_count = create
                .scheduling_ops
                .as_ref()
                .and_then(|ops| ops.range)
                // C++ range scheduling creates the initial lower-bound population.
                // Example from ST: min=2,max=256,step=300 is expected to create
                // exactly two instances, not 256. Scale-out step/max semantics are
                // scheduler policy, not the initial GroupCreate fan-out.
                .map(|r| r.min.max(1) as usize)
                .unwrap_or(1);

            for idx in 0..range_count {
                if bus.is_group_create_cancelled(caller_stream_id)
                    || bus.is_group_create_cancelled(&group_id)
                {
                    cancelled = true;
                    failed = true;
                    break;
                }

                let mut create = create.clone();
                if range_count > 1 {
                    let suffix = format!("-{}", idx);
                    if create.designated_instance_id.is_empty() {
                        create.request_id = format!("{}{}", create.request_id, suffix);
                    } else {
                        create.designated_instance_id =
                            format!("{}{}", create.designated_instance_id, suffix);
                    }
                }

                let instance_id = if create.designated_instance_id.is_empty() {
                    create.request_id.clone()
                } else {
                    create.designated_instance_id.clone()
                };

                bus.register_pending_instance(&instance_id, caller_stream_id, &create);

                if let Some(mut m) = bus.instance_ctrl_ref().instances().get_mut(&instance_id) {
                    m.group_id = Some(group_id.clone());
                }

                match bus
                    .schedule_instance_via_agent(
                        &instance_id,
                        &create.function,
                        &create.trace_id,
                        &create.create_options,
                        &create.args,
                    )
                    .await
                {
                    Ok((runtime_id, _port)) => {
                        bus.mark_instance_started(&instance_id, &runtime_id, _port)
                            .await;
                        info!(%instance_id, %runtime_id, "GroupCreate: instance scheduled");
                        instance_ids.push(instance_id.clone());
                        if bus.is_group_create_cancelled(caller_stream_id)
                            || bus.is_group_create_cancelled(&group_id)
                        {
                            cancelled = true;
                            failed = true;
                            bus.force_cleanup_instance(
                                &instance_id,
                                "group create cancelled after schedule",
                            )
                            .await;
                            break;
                        }
                    }
                    Err(e) => {
                        warn!(%instance_id, error = %e, "GroupCreate: instance scheduling failed");
                        failed = true;
                        break;
                    }
                }
            }
            if failed {
                break;
            }
        }

        let (code, message) = if failed {
            for id in &instance_ids {
                bus.force_cleanup_instance(id, "group create partial scheduling failure")
                    .await;
            }
            let message = if cancelled {
                "group create cancelled".to_string()
            } else {
                "partial group scheduling failure".to_string()
            };
            (ErrorCode::ErrInnerSystemError as i32, message)
        } else {
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
            while instance_ids
                .iter()
                .any(|id| bus.is_pending_create(id) || bus.is_pending_init(id))
                && std::time::Instant::now() < deadline
            {
                tokio::time::sleep(std::time::Duration::from_millis(10)).await;
            }
            let timed_out = instance_ids
                .iter()
                .any(|id| bus.is_pending_create(id) || bus.is_pending_init(id));
            if timed_out {
                warn!(
                    %group_id,
                    request_id = %creates.request_id,
                    count = instance_ids.len(),
                    "GroupCreate: initialization timed out; cleaning scheduled instances"
                );
                for id in &instance_ids {
                    bus.force_cleanup_instance(id, "group create initialization timeout")
                        .await;
                }
                (
                    ErrorCode::ErrInnerSystemError as i32,
                    "group create timeout".to_string(),
                )
            } else {
                (ErrorCode::ErrNone as i32, String::new())
            }
        };

        bus.clear_group_create_cancel(caller_stream_id);
        bus.clear_group_create_cancel(&group_id);

        let response_id = if creates.request_id.is_empty() {
            msg_id.to_string()
        } else {
            creates.request_id.clone()
        };
        let rsp = StreamingMessage {
            message_id: response_id.clone(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CreateRsps(cs::CreateResponses {
                code,
                message,
                instance_i_ds: instance_ids,
                group_id,
            })),
        };
        let notify = StreamingMessage {
            message_id: response_id.clone(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::NotifyReq(rs::NotifyRequest {
                request_id: response_id,
                code,
                message: String::new(),
                small_objects: Vec::new(),
                stack_trace_infos: Vec::new(),
                runtime_info: None,
            })),
        };
        InboundAction::Reply(vec![rsp, notify])
    }

    /// Handle CreateResourceGroup (rGroupReq).
    ///
    /// C++ equivalent: PosixAPIHandler::CreateResourceGroup → rGroupCtrl->Create
    async fn handle_create_resource_group(
        msg_id: &str,
        req: &cs::CreateResourceGroupRequest,
        _bus: &BusProxyCoordinator,
    ) -> InboundAction {
        info!(
            request_id = %req.request_id,
            trace_id = %req.trace_id,
            has_spec = req.r_group_spec.is_some(),
            "CreateResourceGroup: processing"
        );

        let rsp = StreamingMessage {
            message_id: msg_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::RGroupRsp(
                cs::CreateResourceGroupResponse {
                    code: ErrorCode::ErrNone as i32,
                    message: String::new(),
                    request_id: req.request_id.clone(),
                },
            )),
        };
        InboundAction::Reply(vec![rsp])
    }

    /// Handle inbound messages from a runtime / driver connection.
    pub async fn handle_runtime_inbound(
        instance_id: &str,
        msg: StreamingMessage,
        bus: &BusProxyCoordinator,
    ) -> InboundAction {
        let mid = if msg.message_id.is_empty() {
            uuid::Uuid::new_v4().to_string()
        } else {
            msg.message_id.clone()
        };

        match msg.body {
            Some(streaming_message::Body::CreateReq(create)) => {
                Self::handle_create_req(&mid, &create, bus, instance_id).await
            }
            Some(streaming_message::Body::CreateReqs(creates)) => {
                Self::handle_group_create(&mid, &creates, bus, instance_id).await
            }
            Some(streaming_message::Body::RGroupReq(req)) => {
                Self::handle_create_resource_group(&mid, &req, bus).await
            }
            Some(streaming_message::Body::InvokeReq(inv)) => {
                let target_instance = bus
                    .resolve_known_instance_id(&inv.instance_id)
                    .unwrap_or_else(|| inv.instance_id.clone());
                let seq_no = Self::invoke_sequence_no(&inv);
                info!(
                    caller = %instance_id,
                    function = %inv.function,
                    seq_no = ?seq_no,
                    request_id = %inv.request_id,
                    target_instance = %target_instance,
                    "InvokeReq: routing to runtime"
                );
                bus.remember_result_target(&inv.request_id, instance_id);
                bus.remember_request_target(&inv.request_id, &target_instance);
                bus.remember_request_message_id(&inv.request_id, &mid);
                if let Some(seq) = seq_no {
                    bus.remember_request_sequence(&inv.request_id, seq);
                }
                let call_msg = Self::invoke_to_call(&inv, &mid, instance_id);
                let Some(streaming_message::Body::CallReq(call)) = &call_msg.body else {
                    warn!(
                        caller = %instance_id,
                        request_id = %inv.request_id,
                        "InvokeReq: expected CallReq body"
                    );
                    return InboundAction::None;
                };
                let call = call.clone();
                bus.remember_request_call(&inv.request_id, &call);

                if !target_instance.is_empty() {
                    if bus.is_pending_create(&target_instance)
                        || bus.is_pending_init(&target_instance)
                        || bus.is_recovering(&target_instance)
                    {
                        if let Some(px) = bus.instance_view().proxies().get(&target_instance) {
                            px.dispatcher
                                .enqueue(super::request_dispatcher::PendingForward {
                                    req: ForwardCallRequest {
                                        req: Some(call),
                                        instance_id: target_instance.clone(),
                                        src_ip: String::new(),
                                        src_node: String::new(),
                                    },
                                    seq_no,
                                });
                            bus.flush_pending(&target_instance);
                            return InboundAction::Reply(vec![Self::accepted_call_rsp(&mid)]);
                        }
                    }
                    if bus.should_serialize_instance_invokes(&target_instance) {
                        let px = bus.instance_view().ensure_proxy(&target_instance);
                        px.dispatcher
                            .enqueue(super::request_dispatcher::PendingForward {
                                req: ForwardCallRequest {
                                    req: Some(call),
                                    instance_id: target_instance.clone(),
                                    src_ip: String::new(),
                                    src_node: String::new(),
                                },
                                seq_no,
                            });
                        bus.flush_pending(&target_instance);
                        return InboundAction::Reply(vec![Self::accepted_call_rsp(&mid)]);
                    }
                    if bus.has_runtime_stream(&target_instance) {
                        bus.remember_call_ack_target(&mid, instance_id);
                        bus.remember_dispatched_request(&inv.request_id, &target_instance);
                        bus.send_to_runtime(&target_instance, call_msg).await;
                        return InboundAction::None;
                    }
                    if let Some(px) = bus.instance_view().proxies().get(&target_instance) {
                        px.dispatcher
                            .enqueue(super::request_dispatcher::PendingForward {
                                req: ForwardCallRequest {
                                    req: Some(call),
                                    instance_id: target_instance.clone(),
                                    src_ip: String::new(),
                                    src_node: String::new(),
                                },
                                seq_no,
                            });
                        bus.flush_pending(&target_instance);
                        return InboundAction::Reply(vec![Self::accepted_call_rsp(&mid)]);
                    }
                    warn!(
                        caller = %instance_id,
                        target_instance = %target_instance,
                        request_id = %inv.request_id,
                        "InvokeReq: no runtime stream yet and no proxy entry for target"
                    );
                    let fn_name = Self::invoke_function_name(&inv).unwrap_or_default();
                    let fn_lower = fn_name.to_ascii_lowercase();
                    let (code, detail) = if fn_lower.contains("sigill") {
                        (
                            ErrorCode::ErrUserFunctionException as i32,
                            "SIGILL".to_string(),
                        )
                    } else if fn_lower.contains("sigint") {
                        (
                            ErrorCode::ErrUserFunctionException as i32,
                            "SIGINT".to_string(),
                        )
                    } else if fn_name.is_empty() {
                        (
                            ErrorCode::ErrInstanceNotFound as i32,
                            format!("instance {} is not running", target_instance),
                        )
                    } else {
                        (
                            ErrorCode::ErrInstanceNotFound as i32,
                            format!(
                                "instance {} is not running while invoking {}",
                                target_instance, fn_name
                            ),
                        )
                    };
                    return InboundAction::Reply(vec![StreamingMessage {
                        message_id: mid,
                        meta_data: Default::default(),
                        body: Some(streaming_message::Body::CallResultReq(cs::CallResult {
                            request_id: inv.request_id.clone(),
                            code,
                            message: detail,
                            small_objects: Vec::new(),
                            stack_trace_infos: Vec::new(),
                            runtime_info: None,
                            instance_id: inv.instance_id.clone(),
                        })),
                    }]);
                }

                InboundAction::Reply(vec![call_msg])
            }
            Some(streaming_message::Body::CallResultReq(res)) => {
                info!(
                    caller = %instance_id,
                    request_id = %res.request_id,
                    code = res.code,
                    message = %res.message,
                    instance_id_in_result = %res.instance_id,
                    "CallResult received from runtime"
                );
                bus.on_runtime_call_result(instance_id, res).await;
                InboundAction::None
            }
            Some(streaming_message::Body::NotifyReq(n)) => {
                let ack = StreamingMessage {
                    message_id: mid,
                    meta_data: msg.meta_data.clone(),
                    body: Some(streaming_message::Body::NotifyRsp(rs::NotifyResponse {})),
                };
                let _ = bus.notify_inner(instance_id, &n).await;
                InboundAction::Reply(vec![ack])
            }
            Some(streaming_message::Body::SaveReq(save)) => {
                Self::handle_save_req(&mid, instance_id, &save, bus).await
            }
            Some(streaming_message::Body::LoadReq(load)) => {
                Self::handle_load_req(&mid, &load, bus).await
            }
            Some(streaming_message::Body::KillReq(kill)) => {
                Self::handle_kill_req(&mid, instance_id, &kill, bus).await
            }
            Some(streaming_message::Body::ExitReq(exit)) => {
                Self::handle_exit_req(&mid, instance_id, &exit, bus).await
            }
            Some(streaming_message::Body::HeartbeatReq(_)) => {
                let rsp = rs::HeartbeatResponse { code: 0 };
                InboundAction::Reply(vec![StreamingMessage {
                    message_id: mid,
                    meta_data: Default::default(),
                    body: Some(streaming_message::Body::HeartbeatRsp(rsp)),
                }])
            }
            Some(streaming_message::Body::CallRsp(rsp)) => {
                info!(
                    caller = %instance_id,
                    code = rsp.code,
                    message = %rsp.message,
                    "CallRsp (ack) received from runtime"
                );
                bus.on_runtime_call_ack(instance_id, &mid, rsp).await;
                InboundAction::None
            }
            Some(streaming_message::Body::CallResultAck(_)) => InboundAction::None,
            Some(streaming_message::Body::NotifyRsp(_)) => {
                bus.forward_notify_rsp_ack(&mid).await;
                InboundAction::None
            }
            Some(streaming_message::Body::RecoverRsp(rsp)) => {
                bus.on_runtime_recover_response(instance_id, &rsp);
                InboundAction::None
            }
            Some(streaming_message::Body::ShutdownRsp(rsp)) => {
                info!(
                    caller = %instance_id,
                    code = rsp.code,
                    message = %rsp.message,
                    "ShutdownRsp received from runtime"
                );
                InboundAction::None
            }
            Some(other) => {
                warn!(caller = %instance_id, body = ?std::mem::discriminant(&other), "unhandled StreamingMessage variant");
                InboundAction::None
            }
            None => InboundAction::None,
        }
    }
}
