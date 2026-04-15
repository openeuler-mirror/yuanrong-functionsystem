//! Maps runtime `StreamingMessage` variants to the call / result plane.

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

impl InvocationHandler {
    /// Turn a core `InvokeRequest` into a runtime `CallRequest` wrapped as `StreamingMessage`.
    pub fn invoke_to_call(invoke: &cs::InvokeRequest, message_id: &str) -> StreamingMessage {
        let call = rs::CallRequest {
            function: invoke.function.clone(),
            args: invoke.args.clone(),
            trace_id: invoke.trace_id.clone(),
            request_id: invoke.request_id.clone(),
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
            format!("{:032x}", uuid::Uuid::new_v4().as_u128())
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

        bus.register_pending_instance(&instance_id, caller_stream_id, create);

        let start_result = bus.schedule_instance_via_agent(
            &instance_id,
            &create.function,
            &create.trace_id,
        ).await;

        match start_result {
            Ok((runtime_id, runtime_port)) => {
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

        let (code, message) = match signal {
            // True kill signals: actually stop the runtime process.
            1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 => {
                info!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: executing kill"
                );
                bus.execute_kill(&target_id, signal).await
            }
            // Subscription/notification signals: ack without killing.
            SUBSCRIBE_SIGNAL | NOTIFY_SIGNAL | UNSUBSCRIBE_SIGNAL => {
                debug!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: subscription/notify signal, acking without kill"
                );
                (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
            }
            // Checkpoint / suspend / resume: ack without killing.
            INSTANCE_CHECKPOINT_SIGNAL..=GROUP_RESUME_SIGNAL => {
                debug!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: checkpoint/suspend/resume signal, acking without kill"
                );
                (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
            }
            // User-defined signals (≥64): forward to target runtime via its stream.
            s if s >= MIN_USER_SIGNAL => {
                info!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal = s,
                    "KillReq: user signal, forwarding to target"
                );
                bus.forward_user_signal(&target_id, caller_instance_id, kill).await
            }
            _ => {
                warn!(
                    caller = %caller_instance_id,
                    target = %target_id,
                    signal,
                    "KillReq: unexpected signal value"
                );
                (yr_proto::common::ErrorCode::ErrNone as i32, String::new())
            }
        };

        let rsp = cs::KillResponse {
            code,
            message,
            payload: Vec::new(),
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
            bus.apply_instance_exit(caller_instance_id, false, &exit.message).await;
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

        info!(
            %group_id,
            request_id = %creates.request_id,
            count = creates.requests.len(),
            "GroupCreate: batch scheduling"
        );

        for create in &creates.requests {
            let instance_id = if create.designated_instance_id.is_empty() {
                format!("{:032x}", uuid::Uuid::new_v4().as_u128())
            } else {
                create.designated_instance_id.clone()
            };

            bus.register_pending_instance(&instance_id, caller_stream_id, create);

            if let Some(mut m) = bus.instance_ctrl_ref().instances().get_mut(&instance_id) {
                m.group_id = Some(group_id.clone());
            }

            match bus
                .schedule_instance_via_agent(&instance_id, &create.function, &create.trace_id)
                .await
            {
                Ok((runtime_id, _port)) => {
                    info!(%instance_id, %runtime_id, "GroupCreate: instance scheduled");
                    instance_ids.push(instance_id);
                }
                Err(e) => {
                    warn!(%instance_id, error = %e, "GroupCreate: instance scheduling failed");
                    failed = true;
                    break;
                }
            }
        }

        let (code, message) = if failed {
            (
                ErrorCode::ErrInnerSystemError as i32,
                "partial group scheduling failure".to_string(),
            )
        } else {
            (ErrorCode::ErrNone as i32, String::new())
        };

        let rsp = StreamingMessage {
            message_id: msg_id.to_string(),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CreateRsps(cs::CreateResponses {
                code,
                message,
                instance_i_ds: instance_ids,
                group_id,
            })),
        };
        InboundAction::Reply(vec![rsp])
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
                info!(
                    caller = %instance_id,
                    function = %inv.function,
                    request_id = %inv.request_id,
                    target_instance = %inv.instance_id,
                    "InvokeReq: routing to runtime"
                );
                let call_msg = Self::invoke_to_call(&inv, &mid);
                let Some(streaming_message::Body::CallReq(call)) = &call_msg.body else {
                    warn!(
                        caller = %instance_id,
                        request_id = %inv.request_id,
                        "InvokeReq: expected CallReq body"
                    );
                    return InboundAction::None;
                };
                let call = call.clone();

                if !inv.instance_id.is_empty() {
                    if bus.has_runtime_stream(&inv.instance_id) {
                        bus.send_to_runtime(&inv.instance_id, call_msg).await;
                        return InboundAction::None;
                    }
                    if let Some(px) = bus.instance_view().proxies().get(&inv.instance_id) {
                        px.dispatcher.enqueue(super::request_dispatcher::PendingForward {
                            req: ForwardCallRequest {
                                req: Some(call),
                                instance_id: inv.instance_id.clone(),
                                src_ip: String::new(),
                                src_node: String::new(),
                            },
                        });
                        return InboundAction::None;
                    }
                    warn!(
                        caller = %instance_id,
                        target_instance = %inv.instance_id,
                        request_id = %inv.request_id,
                        "InvokeReq: no runtime stream yet and no proxy entry for target"
                    );
                    return InboundAction::None;
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
                InboundAction::None
            }
            Some(streaming_message::Body::CallResultAck(_)) => {
                InboundAction::None
            }
            Some(streaming_message::Body::NotifyRsp(_)) => {
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
