use std::time::Duration;

use tonic::Status;
use tracing::warn;
use yr_proto::internal::{GroupScheduleRequest, GroupScheduleResponse, ScheduleRequest};
use yr_metastore_client::MetaStoreClient;

use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;
use crate::scheduler::SchedulingEngine;

fn schedule_ttl_sec(req: &GroupScheduleRequest) -> Duration {
    let s = req.timeout_sec.max(1) as u64;
    Duration::from_secs(s)
}

/// Optional MetaStore writer for group lifecycle (C++ global metadata parity).
pub struct GroupLifecycle<'a> {
    pub client: &'a mut MetaStoreClient,
    pub key_prefix: &'a str,
}

async fn record_phase(
    life: Option<&mut GroupLifecycle<'_>>,
    group_id: &str,
    body: serde_json::Value,
) -> Result<(), Status> {
    let Some(l) = life else {
        return Ok(());
    };
    let key = format!("{}/group/{}", l.key_prefix.trim_end_matches('/'), group_id);
    let s = body.to_string();
    l.client
        .put(&key, s.as_bytes())
        .await
        .map_err(|e| Status::internal(format!("metastore put: {e}")))?;
    Ok(())
}

/// Gang scheduling: reserve all members, place sequentially, rollback on partial failure.
pub async fn execute_group_schedule(
    engine: &SchedulingEngine,
    resource_view: &ResourceView,
    nodes: &LocalNodeManager,
    mut req: GroupScheduleRequest,
    mut lifecycle: Option<GroupLifecycle<'_>>,
) -> Result<GroupScheduleResponse, Status> {
    let requests: Vec<ScheduleRequest> = std::mem::take(&mut req.requests);
    if requests.is_empty() {
        return Ok(GroupScheduleResponse {
            success: false,
            error_code: 400,
            message: "empty group".into(),
            instance_ids: vec![],
            group_id: req.group_id,
        });
    }

    record_phase(
        lifecycle.as_mut(),
        &req.group_id,
        serde_json::json!({
            "phase": "scheduling",
            "member_count": requests.len(),
            "group_name": req.group_name,
            "same_running_lifecycle": req.same_running_lifecycle,
        }),
    )
    .await?;

    let ttl = schedule_ttl_sec(&req);
    let mut reservations: Vec<(String, String)> = Vec::with_capacity(requests.len());
    let mut plan: Vec<(ScheduleRequest, String)> = Vec::with_capacity(requests.len());

    for (i, sub) in requests.into_iter().enumerate() {
        let pick = engine
            .select_node(&sub, false)
            .ok_or_else(|| Status::resource_exhausted("no node fits group member"))?;
        let rid = format!("gang/{}/{}", req.group_id, i);
        if !resource_view.try_reserve(&pick.node_id, &rid, &sub.required_resources, ttl) {
            for (nid, r) in reservations.iter().rev() {
                resource_view.release_reservation(nid, r);
            }
            record_phase(
                lifecycle.as_mut(),
                &req.group_id,
                serde_json::json!({ "phase": "failed", "reason": "reservation_partial" }),
            )
            .await?;
            return Ok(GroupScheduleResponse {
                success: false,
                error_code: 409,
                message: "gang reservation failed (partial rollback)".into(),
                instance_ids: vec![],
                group_id: req.group_id.clone(),
            });
        }
        reservations.push((pick.node_id.clone(), rid));
        plan.push((sub, pick.node_id));
    }

    let mut instance_ids: Vec<String> = Vec::with_capacity(plan.len());

    for ((sub, node_id), (res_node, _rid)) in plan.iter().zip(reservations.iter()) {
        debug_assert_eq!(node_id, res_node);
        match nodes.forward_schedule(node_id, sub.clone()).await {
            Ok(resp) => {
                if resp.success {
                    instance_ids.push(resp.instance_id);
                } else {
                    warn!(message = %resp.message, %node_id, "group member schedule rejected");
                    for (nid, r) in reservations.iter().rev() {
                        resource_view.release_reservation(nid, r);
                    }
                    record_phase(
                        lifecycle.as_mut(),
                        &req.group_id,
                        serde_json::json!({ "phase": "failed", "reason": "forward_reject", "message": resp.message }),
                    )
                    .await?;
                    return Ok(GroupScheduleResponse {
                        success: false,
                        error_code: resp.error_code,
                        message: resp.message,
                        instance_ids: vec![],
                        group_id: req.group_id.clone(),
                    });
                }
            }
            Err(e) => {
                for (nid, r) in reservations.iter().rev() {
                    resource_view.release_reservation(nid, r);
                }
                record_phase(
                    lifecycle.as_mut(),
                    &req.group_id,
                    serde_json::json!({ "phase": "failed", "reason": "rpc", "message": e.to_string() }),
                )
                .await?;
                return Err(e);
            }
        }
    }

    for (nid, rid) in &reservations {
        resource_view.commit_reservation(nid, rid);
    }

    record_phase(
        lifecycle.as_mut(),
        &req.group_id,
        serde_json::json!({ "phase": "committed", "instance_ids": instance_ids }),
    )
    .await?;

    Ok(GroupScheduleResponse {
        success: true,
        error_code: 0,
        message: "ok".into(),
        instance_ids,
        group_id: req.group_id,
    })
}
