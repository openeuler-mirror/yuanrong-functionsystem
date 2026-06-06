use std::collections::{BTreeMap, HashMap};
use std::sync::Arc;

use axum::extract::{Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use axum08 as axum_compat;
use prost::Message;
use serde::Deserialize;
use serde_json::{json, Value};
use yr_metastore_client::MetaStoreClient;
use yr_proto::common::ErrorCode;
use yr_proto::messages::{
    BundleInfo, CommonStatus, DeleteSnapshotRequest, DeleteSnapshotResponse,
    ListSnapshotsByFunctionKeyRequest, ListSnapshotsByFunctionKeyResponse,
    ListSnapshotsByTenantRequest, ListSnapshotsByTenantResponse, QueryInstancesInfoResponse,
    QueryResourceGroupRequest, QueryResourceGroupResponse, ResourceGroupInfo, ResourceInfo,
};
use yr_proto::resources::{
    value::{Scalar, Type as ValueType},
    InstanceInfo, InstanceStatus, Resource, Resources, ResourceUnit,
};

const TYPE_JSON: &str = "json";
const RESOURCE_GROUP_KEY_PREFIX: &str = "/yr/resourcegroup";

fn is_json_type(val: Option<&str>) -> bool {
    match val {
        None => true,
        Some(v) => v == TYPE_JSON || v.contains("json"),
    }
}

fn parse_type_header(headers: &HeaderMap) -> bool {
    is_json_type(headers.get("Type").and_then(|v| v.to_str().ok()))
}
const TYPE_PROTOBUF: &str = "protobuf";

use crate::scheduler::MasterState;
use crate::snapshot::{snapshot_to_proto, snapshots_to_proto_bytes};

#[derive(Clone)]
pub struct HttpState {
    pub master: Arc<MasterState>,
    pub metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
}

#[derive(Debug, Deserialize)]
pub struct EvictBody {
    #[serde(alias = "node_id")]
    pub agentid: String,
    #[serde(default)]
    pub timeoutsec: i64,
    #[serde(default)]
    pub reason: String,
}

#[derive(Debug, Deserialize, Default)]
pub struct ExploreBody {
    #[serde(default)]
    pub prefix: Option<String>,
}

fn json_pick_str(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> String {
    for k in keys {
        if let Some(Value::String(s)) = obj.get(*k) {
            return s.clone();
        }
    }
    String::new()
}

fn json_pick_i32(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> Option<i32> {
    for k in keys {
        if let Some(v) = obj.get(*k) {
            if let Some(i) = v.as_i64() {
                return Some(i as i32);
            }
            if let Some(f) = v.as_f64() {
                return Some(f as i32);
            }
        }
    }
    None
}

fn json_pick_i64(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> Option<i64> {
    for k in keys {
        if let Some(v) = obj.get(*k) {
            if let Some(i) = v.as_i64() {
                return Some(i);
            }
            if let Some(f) = v.as_f64() {
                return Some(f as i64);
            }
        }
    }
    None
}

fn instance_status_from_json(v: Option<&Value>) -> Option<InstanceStatus> {
    let o = v?.as_object()?;
    Some(InstanceStatus {
        code: json_pick_i32(o, &["code"]).unwrap_or(0),
        exit_code: json_pick_i32(o, &["exitCode", "exit_code"]).unwrap_or(0),
        msg: json_pick_str(o, &["msg", "message"]),
        r#type: json_pick_i32(o, &["type"]).unwrap_or(0),
        err_code: json_pick_i32(o, &["errCode", "err_code"]).unwrap_or(0),
    })
}

fn json_to_resource_entry(name: &str, v: &Value) -> Option<Resource> {
    let obj = v.as_object()?;
    let res_name = obj
        .get("name")
        .and_then(|x| x.as_str())
        .unwrap_or(name)
        .to_string();
    let scalar_obj = obj.get("scalar")?.as_object()?;
    let val = scalar_obj
        .get("value")
        .and_then(|x| x.as_f64())
        .or_else(|| {
            scalar_obj
                .get("value")
                .and_then(|x| x.as_i64())
                .map(|i| i as f64)
        })?;
    Some(Resource {
        name: res_name,
        r#type: ValueType::Scalar as i32,
        scalar: Some(Scalar { value: val, limit: 0.0 }),
        ..Default::default()
    })
}

fn json_value_to_resources(v: &Value) -> Option<Resources> {
    let obj = v.as_object()?;
    let map_obj = if let Some(inner) = obj.get("resources").and_then(|x| x.as_object()) {
        inner
    } else {
        obj
    };
    let mut resources = HashMap::new();
    for (name, rv) in map_obj {
        if let Some(res) = json_to_resource_entry(name, rv) {
            resources.insert(name.clone(), res);
        }
    }
    if resources.is_empty() {
        None
    } else {
        Some(Resources { resources })
    }
}

fn instance_resources_from_json(v: &Value) -> Option<Resources> {
    let r = v.get("resources")?;
    json_value_to_resources(r)
}

fn json_to_instance_info(inst_id: &str, v: &Value) -> InstanceInfo {
    let Some(o) = v.as_object() else {
        return InstanceInfo {
            instance_id: inst_id.to_string(),
            ..Default::default()
        };
    };
    let instance_id = json_pick_str(o, &["instanceID", "instance_id", "id"]);
    let instance_id = if instance_id.is_empty() {
        inst_id.to_string()
    } else {
        instance_id
    };
    InstanceInfo {
        instance_id,
        request_id: json_pick_str(o, &["requestID", "request_id"]),
        runtime_id: json_pick_str(o, &["runtimeID", "runtime_id"]),
        runtime_address: json_pick_str(o, &["runtimeAddress", "runtime_address"]),
        function_agent_id: json_pick_str(o, &["functionAgentID", "function_agent_id"]),
        function_proxy_id: json_pick_str(o, &["functionProxyID", "function_proxy_id"]),
        function: json_pick_str(o, &["function"]),
        job_id: json_pick_str(o, &["jobID", "job_id"]),
        parent_id: json_pick_str(o, &["parentID", "parent_id"]),
        tenant_id: json_pick_str(o, &["tenantID", "tenant_id", "tenant"]),
        deploy_times: json_pick_i32(o, &["deployTimes", "deploy_times"]).unwrap_or(1),
        schedule_times: json_pick_i32(o, &["scheduleTimes", "schedule_times"]).unwrap_or(0),
        resources: instance_resources_from_json(v),
        instance_status: instance_status_from_json(
            v.get("instanceStatus").or_else(|| v.get("instance_status")),
        ),
        version: json_pick_i64(o, &["version"]).unwrap_or(0),
        ..Default::default()
    }
}

fn collect_sorted_instance_infos(list_json: &str) -> Vec<InstanceInfo> {
    let Ok(map) = serde_json::from_str::<serde_json::Map<String, Value>>(list_json) else {
        return vec![];
    };
    let mut pairs: Vec<_> = map.into_iter().collect();
    pairs.sort_by(|a, b| a.0.cmp(&b.0));
    pairs
        .into_iter()
        .map(|(id, v)| json_to_instance_info(&id, &v))
        .collect()
}

fn index_instance_by_request_id(list_json: &str) -> HashMap<String, InstanceInfo> {
    let mut m = HashMap::new();
    let Ok(map) = serde_json::from_str::<serde_json::Map<String, Value>>(list_json) else {
        return m;
    };
    for (id, v) in map {
        let info = json_to_instance_info(&id, &v);
        if !info.request_id.is_empty() {
            m.insert(info.request_id.clone(), info);
        }
    }
    m
}

fn scheduling_queue_instance_infos(st: &MasterState) -> Vec<InstanceInfo> {
    let body = st.instances.list_json();
    let by_rid = index_instance_by_request_id(&body);
    let q = st.scheduling_queue.lock();
    q.iter()
        .filter_map(|rid| by_rid.get(rid).cloned())
        .collect()
}

fn protobuf_http_response(buf: Vec<u8>) -> Response {
    (
        StatusCode::OK,
        [("content-type", "application/x-protobuf")],
        buf,
    )
        .into_response()
}

fn current_master_address(st: &MasterState) -> String {
    format!("{}:{}", st.config.host, st.config.port)
}

fn current_meta_store_address(st: &MasterState) -> String {
    let configured = st.config.meta_store_address.trim();
    if !configured.is_empty() {
        configured.to_string()
    } else {
        format!("127.0.0.1:{}", st.config.meta_store_port)
    }
}

fn schedule_topology_json(st: &MasterState) -> Value {
    let mut topo = serde_json::Map::from_iter([("members".into(), Value::Array(Vec::new()))]);
    let Some(root) = st.topology.sched_tree().get_root_node() else {
        return Value::Object(topo);
    };

    if let Some(parent) = root.parent() {
        topo.insert(
            "leader".into(),
            json!({
                "name": parent.name(),
                "address": parent.address(),
            }),
        );
    }

    let mut members: Vec<_> = root.children().into_values().collect();
    members.sort_by(|a, b| a.name().cmp(&b.name()));
    topo.insert(
        "members".into(),
        Value::Array(
            members
                .into_iter()
                .map(|node| {
                    json!({
                        "name": node.name(),
                        "address": node.address(),
                    })
                })
                .collect(),
        ),
    );

    Value::Object(topo)
}

fn master_info_type_supported(headers: &HeaderMap) -> bool {
    headers
        .get("Type")
        .and_then(|v| v.to_str().ok())
        .is_none_or(|v| v == TYPE_JSON)
}

fn master_info_payload(st: &MasterState) -> Value {
    json!({
        "master_address": current_master_address(st),
        "meta_store_address": current_meta_store_address(st),
        "schedule_topo": schedule_topology_json(st),
    })
}

pub fn build_grpc_compat_router(state: Arc<MasterState>) -> axum_compat::Router {
    let master_info_state = state;
    axum_compat::Router::new()
        .route("/healthy", axum_compat::routing::get(|| async { "" }))
        .route(
            "/global-scheduler/healthy",
            axum_compat::routing::get(|| async { "" }),
        )
        .route(
            "/masterinfo",
            axum_compat::routing::get({
                let state = master_info_state.clone();
                move |headers: axum_compat::http::HeaderMap| {
                    let state = state.clone();
                    async move {
                        if !master_info_type_supported(&headers) {
                            return axum_compat::response::IntoResponse::into_response(
                                axum_compat::http::StatusCode::BAD_REQUEST,
                            );
                        }
                        axum_compat::response::IntoResponse::into_response(axum_compat::Json(
                            master_info_payload(&state),
                        ))
                    }
                }
            }),
        )
        .route(
            "/global-scheduler/masterinfo",
            axum_compat::routing::get({
                let state = master_info_state;
                move |headers: axum_compat::http::HeaderMap| {
                    let state = state.clone();
                    async move {
                        if !master_info_type_supported(&headers) {
                            return axum_compat::response::IntoResponse::into_response(
                                axum_compat::http::StatusCode::BAD_REQUEST,
                            );
                        }
                        axum_compat::response::IntoResponse::into_response(axum_compat::Json(
                            master_info_payload(&state),
                        ))
                    }
                }
            }),
        )
}

pub fn build_router(
    state: Arc<MasterState>,
    metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
) -> Router {
    let s = HttpState {
        master: state,
        metastore,
    };
    Router::new()
        .route("/healthy", get(healthy))
        .route("/global-scheduler/healthy", get(healthy))
        .route("/queryagents", get(query_agents))
        .route("/global-scheduler/queryagents", get(query_agents))
        .route("/queryagentcount", get(query_agent_count))
        .route("/global-scheduler/queryagentcount", get(query_agent_count))
        .route("/evictagent", post(evict_agent))
        .route("/masterinfo", get(master_info))
        .route("/global-scheduler/masterinfo", get(master_info))
        .route("/resources", get(resources))
        .route("/global-scheduler/resources", get(resources))
        .route("/instance-manager/resources", get(resources))
        .route(
            "/node/localschedulingstatus",
            post(mark_local_scheduler_evicting).delete(clear_local_scheduler_evicting),
        )
        .route("/scheduling_queue", get(scheduling_queue))
        .route("/global-scheduler/scheduling_queue", get(scheduling_queue))
        .route("/named-ins", get(query_named_instances))
        .route("/instance-manager/named-ins", get(query_named_instances))
        .route("/queryinstances", get(query_instances))
        .route("/global-scheduler/queryinstances", get(query_instances))
        .route("/instance-manager/queryinstances", get(query_instances))
        .route("/query-debug-instances", get(query_debug_instances))
        .route("/query-tenant-instances", get(query_tenant_instances))
        .route("/query-group-instances", get(query_group_instances))
        .route("/query-snapshot", get(query_snapshot).post(query_snapshot))
        .route("/list-snapshots", get(list_snapshots).post(list_snapshots))
        .route(
            "/list-snapshots-by-function-key",
            post(list_snapshots_by_function_key),
        )
        .route("/list-snapshots-by-tenant", post(list_snapshots_by_tenant))
        .route("/delete-snapshot", post(delete_snapshot))
        .route("/rgroup", post(query_resource_group))
        .route("/resource-group/rgroup", post(query_resource_group))
        .route("/metastore/explore", post(metastore_explore))
        .with_state(s)
}

fn healthy_probe_header<'a>(headers: &'a HeaderMap, name: &str) -> Option<&'a str> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
}

async fn healthy(State(st): State<HttpState>, headers: HeaderMap) -> Response {
    let node_hdr = healthy_probe_header(&headers, "node-id");
    let pid_hdr = healthy_probe_header(&headers, "pid");

    if node_hdr.is_none() && pid_hdr.is_none() {
        return (StatusCode::OK, "").into_response();
    }

    let expected_node = st.master.config.node_id.as_str();
    let node_ok = node_hdr.is_some_and(|v| v == expected_node);
    if !node_ok {
        return (StatusCode::BAD_REQUEST, "error nodeID").into_response();
    }
    let pid = std::process::id();
    let pid_ok = pid_hdr
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    (StatusCode::OK, "").into_response()
}

async fn scheduling_queue(
    State(st): State<HttpState>,
    headers: HeaderMap,
) -> Response {
    let use_json = parse_type_header(&headers);

    let items: Vec<String> = {
        let q = st.master.scheduling_queue.lock();
        q.iter().cloned().collect()
    };

    if use_json {
        Json(json!({ "queue": items, "len": items.len() })).into_response()
    } else {
        let instance_infos = scheduling_queue_instance_infos(&st.master);
        let resp = QueryInstancesInfoResponse {
            request_id: String::new(),
            code: ErrorCode::ErrNone as i32,
            instance_infos,
        };
        protobuf_http_response(resp.encode_to_vec())
    }
}

async fn metastore_explore(
    State(st): State<HttpState>,
    Json(body): Json<ExploreBody>,
) -> Result<Json<serde_json::Value>, StatusCode> {
    let Some(ms) = &st.metastore else {
        return Err(StatusCode::NOT_FOUND);
    };
    let logical = body.prefix.unwrap_or_else(|| "/".to_string());
    let mut g = ms.lock().await;
    let r = g
        .get_prefix(logical.as_str())
        .await
        .map_err(|_| StatusCode::BAD_GATEWAY)?;
    let kvs: Vec<serde_json::Value> = r
        .kvs
        .iter()
        .map(|kv| {
            json!({
                "key": String::from_utf8_lossy(&kv.key),
                "value_len": kv.value.len(),
            })
        })
        .collect();
    Ok(Json(json!({
        "count": r.count,
        "header_revision": r.header_revision,
        "kvs": kvs,
    })))
}

async fn query_agents(State(st): State<HttpState>) -> Json<serde_json::Value> {
    let body = st.master.topology.list_agents_json("");
    Json(serde_json::from_str(&body).unwrap_or_else(|_| json!([])))
}

async fn query_agent_count(State(st): State<HttpState>) -> String {
    st.master.topology.agent_count().to_string()
}

async fn evict_agent(
    State(st): State<HttpState>,
    Json(body): Json<EvictBody>,
) -> Result<Json<serde_json::Value>, StatusCode> {
    if !st.master.require_leader() {
        return Err(StatusCode::SERVICE_UNAVAILABLE);
    }
    if body.agentid.is_empty() {
        return Err(StatusCode::BAD_REQUEST);
    }
    let grace_sec = if body.timeoutsec > 0 {
        body.timeoutsec
    } else {
        st.master.config.grace_period_seconds as i64
    }
    .max(1) as u64;
    let _ = st
        .master
        .local_sched_mgr
        .evict_agent_with_ack(
            &body.agentid,
            &body.reason,
            std::time::Duration::from_secs(grace_sec),
        )
        .await;
    let ok = st.master.topology.evict(&body.agentid).await;
    if ok {
        st.master.node_manager.remove(&body.agentid);
        st.master
            .schedule_decision
            .apply_topology_resources(&st.master.topology);
    }
    let (code, message) = if ok {
        (0, "success".to_string())
    } else {
        (1, format!("agent {} not found", body.agentid))
    };
    Ok(Json(json!({ "code": code, "message": message })))
}

/// C++ instance_manager_driver.h: QueryNamedInsHandler
/// Returns instances with designated (named) instance IDs.
async fn query_named_instances(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Response {
    let request_id = body
        .and_then(|b| String::from_utf8(b.to_vec()).ok())
        .unwrap_or_else(|| uuid::Uuid::new_v4().to_string());

    let (instances, rid) = st.master.instances.query_named_instances(&request_id);

    let use_json = match headers.get("Content-Type").and_then(|v| v.to_str().ok()) {
        None | Some("application/json") => true,
        Some(_) => false,
    };

    if use_json {
        Json(json!({
            "requestID": rid,
            "instances": instances,
            "count": instances.len(),
        }))
        .into_response()
    } else {
        let instance_infos: Vec<InstanceInfo> = instances
            .into_iter()
            .map(|v| json_to_instance_info("", &v))
            .collect();
        let resp = QueryInstancesInfoResponse {
            request_id: rid,
            code: ErrorCode::ErrNone as i32,
            instance_infos,
        };
        protobuf_http_response(resp.encode_to_vec())
    }
}

/// C++ instance_manager_driver.h: QueryInstancesHandler
async fn query_instances(
    State(st): State<HttpState>,
    headers: HeaderMap,
) -> Response {
    let use_json = parse_type_header(&headers);

    let body = st.master.instances.list_json();

    if use_json {
        let v: serde_json::Value =
            serde_json::from_str(&body).unwrap_or_else(|_| json!({}));
        Json(v).into_response()
    } else {
        let instance_infos = collect_sorted_instance_infos(&body);
        let resp = QueryInstancesInfoResponse {
            request_id: String::new(),
            code: ErrorCode::ErrNone as i32,
            instance_infos,
        };
        protobuf_http_response(resp.encode_to_vec())
    }
}

/// C++ instance_manager_driver.h: QueryDebugInstancesHandler
async fn query_debug_instances(
    State(st): State<HttpState>,
    headers: HeaderMap,
) -> Response {
    let use_json = parse_type_header(&headers);

    let items = st.master.instances.query_debug_instances();

    if use_json {
        Json(json!({ "instances": items, "count": items.len() })).into_response()
    } else {
        let instance_infos: Vec<InstanceInfo> = items
            .into_iter()
            .map(|v| json_to_instance_info("", &v))
            .collect();
        let resp = QueryInstancesInfoResponse {
            request_id: String::new(),
            code: ErrorCode::ErrNone as i32,
            instance_infos,
        };
        protobuf_http_response(resp.encode_to_vec())
    }
}

#[derive(Debug, Deserialize)]
pub struct TenantQuery {
    pub tenant_id: Option<String>,
    pub instance_id: Option<String>,
}

/// C++ instance_manager_driver.h: QueryTenantInstancesHandler
async fn query_tenant_instances(
    State(st): State<HttpState>,
    Query(q): Query<TenantQuery>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<serde_json::Value>)> {
    let tenant_id = q.tenant_id.ok_or_else(|| {
        (
            StatusCode::BAD_REQUEST,
            Json(json!({ "error": "Missing tenant_id parameter" })),
        )
    })?;

    let (instances, count) =
        st.master
            .instances
            .query_by_tenant(&tenant_id, q.instance_id.as_deref());

    let is_system_tenant = tenant_id == st.master.config.cluster_id;

    let mut resp = json!({
        "instances": instances,
        "count": count,
        "tenantID": tenant_id,
    });
    if is_system_tenant {
        resp["isSystemTenant"] = json!(true);
    }
    if let Some(iid) = q.instance_id {
        resp["instanceID"] = json!(iid);
    }
    Ok(Json(resp))
}

/// C++ snap_manager_driver.h: QuerySnapshotHandler — body is `instance_id`.
async fn query_snapshot(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Result<Response, StatusCode> {
    let instance_id = body
        .and_then(|b| String::from_utf8(b.to_vec()).ok())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .ok_or(StatusCode::BAD_REQUEST)?;

    let use_json = parse_type_header(&headers);

    let snap = st.master.snapshots.get(instance_id.as_str());

    if use_json {
        let Some(s) = snap else {
            return Err(StatusCode::NOT_FOUND);
        };
        Ok(Json(s).into_response())
    } else if let Some(s) = snap {
        Ok((
            StatusCode::OK,
            [("content-type", "application/x-protobuf")],
            snapshot_to_proto(&s).encode_to_vec(),
        )
            .into_response())
    } else {
        Err(StatusCode::NOT_FOUND)
    }
}

async fn master_info(
    State(st): State<HttpState>,
    headers: HeaderMap,
) -> Result<Json<Value>, StatusCode> {
    if !master_info_type_supported(&headers) {
        return Err(StatusCode::BAD_REQUEST);
    }

    Ok(Json(master_info_payload(&st.master)))
}

#[derive(Debug, Deserialize, Default)]
struct ListSnapshotsJsonBody {
    #[serde(default)]
    function: Option<String>,
    #[serde(default, alias = "functionName")]
    function_name: Option<String>,
    #[serde(default, alias = "tenantID")]
    tenant_id: Option<String>,
}

#[derive(Debug, Deserialize, Default)]
pub struct ListSnapshotsQuery {
    #[serde(default, alias = "tenantID")]
    pub tenant_id: Option<String>,
}

#[derive(Debug, Deserialize, Default)]
pub struct GroupInstancesQuery {
    #[serde(default, alias = "groupID")]
    pub group_id: Option<String>,
}

/// Query instances that share a scheduling group id (JSON `group_id` / `groupID` / `groupId`).
async fn query_group_instances(
    State(st): State<HttpState>,
    Query(q): Query<GroupInstancesQuery>,
) -> Result<Json<serde_json::Value>, (StatusCode, Json<serde_json::Value>)> {
    let group_id = q.group_id.ok_or_else(|| {
        (
            StatusCode::BAD_REQUEST,
            Json(json!({ "error": "Missing group_id parameter" })),
        )
    })?;
    let group_id = group_id.trim();
    if group_id.is_empty() {
        return Err((
            StatusCode::BAD_REQUEST,
            Json(json!({ "error": "Empty group_id" })),
        ));
    }
    let instances = st.master.instances.query_by_group(group_id);
    let count = instances.len();
    Ok(Json(json!({
        "group_id": group_id,
        "instances": instances,
        "count": count,
    })))
}

fn parse_list_snapshots_payload(raw: &[u8]) -> Result<(String, Option<String>), StatusCode> {
    let s = std::str::from_utf8(raw).map_err(|_| StatusCode::BAD_REQUEST)?;
    let trimmed = s.trim();
    if trimmed.starts_with('{') {
        let j: ListSnapshotsJsonBody =
            serde_json::from_str(trimmed).map_err(|_| StatusCode::BAD_REQUEST)?;
        let fname = j
            .function
            .or(j.function_name)
            .map(|x| x.trim().to_string())
            .filter(|x| !x.is_empty())
            .ok_or(StatusCode::BAD_REQUEST)?;
        let tid = j
            .tenant_id
            .map(|x| x.trim().to_string())
            .filter(|x| !x.is_empty());
        return Ok((fname, tid));
    }
    if trimmed.is_empty() {
        return Err(StatusCode::BAD_REQUEST);
    }
    Ok((trimmed.to_string(), None))
}

/// C++ snap_manager_driver.h: ListSnapshotsHandler — body is `function_name` or JSON
/// `{"function": "...", "tenantID": "..."}`; optional `tenant_id` query overrides / supplements tenant filter.
async fn list_snapshots(
    State(st): State<HttpState>,
    Query(q): Query<ListSnapshotsQuery>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Result<Response, StatusCode> {
    let raw = body.as_deref().unwrap_or_default();
    let (function_name, tenant_from_body) = parse_list_snapshots_payload(raw)?;
    let tenant_filter = q
        .tenant_id
        .as_deref()
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .or_else(|| {
            tenant_from_body
                .as_deref()
                .map(str::trim)
                .filter(|s| !s.is_empty())
        });

    let use_json = parse_type_header(&headers);

    let list = st
        .master
        .snapshots
        .list_by_function_and_tenant(&function_name, tenant_filter);

    if use_json {
        Ok(Json(list).into_response())
    } else {
        Ok((
            StatusCode::OK,
            [("content-type", "application/x-protobuf")],
            snapshots_to_proto_bytes(&list),
        )
            .into_response())
    }
}

async fn list_snapshots_by_function_key(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Result<Response, StatusCode> {
    let raw = body.ok_or(StatusCode::BAD_REQUEST)?;
    let req =
        ListSnapshotsByFunctionKeyRequest::decode(raw.as_ref()).map_err(|_| StatusCode::BAD_REQUEST)?;
    let fk = req.function_key.unwrap_or_default();
    let checkpoint_ids = st.master.snapshots.list_checkpoint_ids_by_function_key(
        fk.tenant_id.as_str(),
        fk.function_type.as_str(),
        fk.namespace.as_str(),
    );
    let use_json = parse_type_header(&headers);
    if use_json {
        Ok(Json(json!({
            "message": "success",
            "requestID": req.request_id,
            "checkpointIDs": checkpoint_ids,
        }))
        .into_response())
    } else {
        Ok(protobuf_http_response(
            ListSnapshotsByFunctionKeyResponse {
                code: ErrorCode::ErrNone as i32,
                message: "success".into(),
                request_id: req.request_id,
                checkpoint_i_ds: checkpoint_ids,
            }
            .encode_to_vec(),
        ))
    }
}

async fn list_snapshots_by_tenant(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Result<Response, StatusCode> {
    let raw = body.ok_or(StatusCode::BAD_REQUEST)?;
    let req = ListSnapshotsByTenantRequest::decode(raw.as_ref()).map_err(|_| StatusCode::BAD_REQUEST)?;
    let checkpoint_ids = st
        .master
        .snapshots
        .list_checkpoint_ids_by_tenant(req.tenant_id.as_str());
    let use_json = parse_type_header(&headers);
    if use_json {
        Ok(Json(json!({
            "message": "success",
            "requestID": req.request_id,
            "checkpointIDs": checkpoint_ids,
        }))
        .into_response())
    } else {
        Ok(protobuf_http_response(
            ListSnapshotsByTenantResponse {
                code: ErrorCode::ErrNone as i32,
                message: "success".into(),
                request_id: req.request_id,
                checkpoint_i_ds: checkpoint_ids,
            }
            .encode_to_vec(),
        ))
    }
}

async fn delete_snapshot(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Result<Response, StatusCode> {
    let raw = body.ok_or(StatusCode::BAD_REQUEST)?;
    let req = DeleteSnapshotRequest::decode(raw.as_ref()).map_err(|_| StatusCode::BAD_REQUEST)?;
    let _ = st.master.snapshots.remove(req.checkpoint_id.as_str());
    let use_json = parse_type_header(&headers);
    if use_json {
        Ok(Json(json!({
            "requestID": req.request_id,
        }))
        .into_response())
    } else {
        Ok(protobuf_http_response(
            DeleteSnapshotResponse {
                code: ErrorCode::ErrNone as i32,
                message: String::new(),
                request_id: req.request_id,
            }
            .encode_to_vec(),
        ))
    }
}

#[derive(Debug, Deserialize, Default)]
pub struct ResourceGroupQuery {
    #[serde(default, alias = "requestID")]
    pub request_id: Option<String>,
    #[serde(default, alias = "rGroupName")]
    pub r_group_name: Option<String>,
}

#[derive(Debug, Deserialize, Default)]
pub struct LocalSchedulingStatusQuery {
    #[serde(default, alias = "nodeID")]
    pub node_id: Option<String>,
}

fn common_status_from_json(v: Option<&Value>) -> Option<CommonStatus> {
    let o = v?.as_object()?;
    Some(CommonStatus {
        code: json_pick_i32(o, &["code"]).unwrap_or(0),
        message: json_pick_str(o, &["msg", "message"]),
    })
}

fn json_pick_group_name(obj: &serde_json::Map<String, Value>) -> String {
    json_pick_str(
        obj,
        &[
            "rGroupName",
            "r_group_name",
            "rgroupname",
            "group_id",
            "groupID",
            "groupId",
            "r_group_id",
            "rGroupId",
        ],
    )
}

fn json_to_bundle_info(instance_id: &str, group_name: &str, v: &Value) -> BundleInfo {
    let Some(o) = v.as_object() else {
        return BundleInfo {
            bundle_id: instance_id.to_string(),
            r_group_name: group_name.to_string(),
            ..Default::default()
        };
    };
    BundleInfo {
        bundle_id: instance_id.to_string(),
        r_group_name: group_name.to_string(),
        parent_r_group_name: json_pick_str(o, &["parentRGroupName", "parent_r_group_name"]),
        function_proxy_id: json_pick_str(o, &["functionProxyID", "function_proxy_id"]),
        function_agent_id: json_pick_str(o, &["functionAgentID", "function_agent_id"]),
        tenant_id: json_pick_str(o, &["tenantID", "tenant_id", "tenant"]),
        resources: instance_resources_from_json(v),
        labels: vec![],
        status: common_status_from_json(
            v.get("instanceStatus").or_else(|| v.get("instance_status")),
        ),
        parent_id: json_pick_str(o, &["parentID", "parent_id"]),
        kv_labels: HashMap::new(),
    }
}

fn collect_resource_group_infos(
    list_json: &str,
    filter_name: &str,
    request_id: &str,
) -> Vec<ResourceGroupInfo> {
    let Ok(map) = serde_json::from_str::<serde_json::Map<String, Value>>(list_json) else {
        return vec![];
    };
    let mut grouped: BTreeMap<String, Vec<(String, Value)>> = BTreeMap::new();
    for (instance_id, value) in map {
        let Some(obj) = value.as_object() else {
            continue;
        };
        let group_name = json_pick_group_name(obj);
        if group_name.is_empty() {
            continue;
        }
        if !filter_name.is_empty() && group_name != filter_name {
            continue;
        }
        grouped
            .entry(group_name)
            .or_default()
            .push((instance_id, value));
    }

    grouped
        .into_iter()
        .map(|(group_name, mut instances)| {
            instances.sort_by(|a, b| a.0.cmp(&b.0));
            let tenant_id = instances
                .iter()
                .find_map(|(_, value)| {
                    value
                        .as_object()
                        .map(|o| json_pick_str(o, &["tenantID", "tenant_id", "tenant"]))
                        .filter(|v| !v.is_empty())
                })
                .unwrap_or_default();
            let status = instances
                .iter()
                .find_map(|(_, value)| {
                    common_status_from_json(
                        value
                            .get("instanceStatus")
                            .or_else(|| value.get("instance_status")),
                    )
                })
                .or(Some(CommonStatus {
                    code: 0,
                    message: String::new(),
                }));
            let bundles = instances
                .into_iter()
                .map(|(instance_id, value)| json_to_bundle_info(&instance_id, &group_name, &value))
                .collect();
            ResourceGroupInfo {
                name: group_name,
                owner: String::new(),
                app_id: String::new(),
                tenant_id,
                bundles,
                status,
                parent_id: String::new(),
                request_id: request_id.to_string(),
                trace_id: String::new(),
                opt: None,
            }
        })
        .collect()
}

fn resource_group_map_key(info: &ResourceGroupInfo) -> String {
    format!("{}\u{0}{}", info.tenant_id, info.name)
}

fn merge_resource_group_info(
    mut persisted: ResourceGroupInfo,
    observed: &ResourceGroupInfo,
) -> ResourceGroupInfo {
    if persisted.name.is_empty() {
        persisted.name = observed.name.clone();
    }
    if persisted.owner.is_empty() {
        persisted.owner = observed.owner.clone();
    }
    if persisted.app_id.is_empty() {
        persisted.app_id = observed.app_id.clone();
    }
    if persisted.tenant_id.is_empty() {
        persisted.tenant_id = observed.tenant_id.clone();
    }
    if persisted.parent_id.is_empty() {
        persisted.parent_id = observed.parent_id.clone();
    }
    if persisted.request_id.is_empty() {
        persisted.request_id = observed.request_id.clone();
    }
    if persisted.trace_id.is_empty() {
        persisted.trace_id = observed.trace_id.clone();
    }
    if persisted.opt.is_none() {
        persisted.opt = observed.opt.clone();
    }
    if persisted.bundles.is_empty() && !observed.bundles.is_empty() {
        persisted.bundles = observed.bundles.clone();
    } else if !observed.bundles.is_empty() {
        persisted.bundles = observed.bundles.clone();
    }
    if (persisted.status.is_none() || !observed.bundles.is_empty()) && observed.status.is_some() {
        persisted.status = observed.status.clone();
    }
    persisted
}

async fn load_resource_group_infos_from_metastore(
    metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
    filter_name: &str,
) -> Vec<ResourceGroupInfo> {
    let Some(store) = metastore else {
        return vec![];
    };
    let mut guard = store.lock().await;
    let Ok(resp) = guard.get_prefix(RESOURCE_GROUP_KEY_PREFIX).await else {
        return vec![];
    };
    let mut out: Vec<ResourceGroupInfo> = resp
        .kvs
        .into_iter()
        .filter_map(|kv| ResourceGroupInfo::decode(kv.value.as_slice()).ok())
        .filter(|info| filter_name.is_empty() || info.name == filter_name)
        .collect();
    out.sort_by(|a, b| a.name.cmp(&b.name).then(a.tenant_id.cmp(&b.tenant_id)));
    out
}

fn common_status_to_json(status: Option<&CommonStatus>) -> Value {
    let status = status.cloned().unwrap_or(CommonStatus {
        code: 0,
        message: String::new(),
    });
    json!({
        "code": status.code,
        "message": status.message,
    })
}

fn resources_to_json(resources: Option<&Resources>) -> Value {
    let Some(resources) = resources else {
        return json!({ "resources": {} });
    };
    let mut out = serde_json::Map::new();
    for (name, resource) in &resources.resources {
        let scalar = resource.scalar.as_ref().map(|s| json!({ "value": s.value }));
        out.insert(
            name.clone(),
            json!({
                "name": resource.name,
                "type": resource.r#type,
                "scalar": scalar,
            }),
        );
    }
    json!({ "resources": Value::Object(out) })
}

fn resource_group_info_to_json(info: &ResourceGroupInfo) -> Value {
    let bundles: Vec<Value> = info
        .bundles
        .iter()
        .map(|bundle| {
            json!({
                "bundleID": bundle.bundle_id,
                "rGroupName": bundle.r_group_name,
                "parentRGroupName": bundle.parent_r_group_name,
                "functionProxyID": bundle.function_proxy_id,
                "functionAgentID": bundle.function_agent_id,
                "tenantID": bundle.tenant_id,
                "resources": resources_to_json(bundle.resources.as_ref()),
                "labels": bundle.labels,
                "status": common_status_to_json(bundle.status.as_ref()),
                "parentId": bundle.parent_id,
                "kvLabels": bundle.kv_labels,
            })
        })
        .collect();
    let opt_json = info
        .opt
        .as_ref()
        .map(|opt| {
            json!({
                "priority": opt.priority,
                "groupPolicy": opt.group_policy,
                "extension": opt.extension,
            })
        })
        .unwrap_or(Value::Null);
    json!({
        "name": info.name,
        "owner": info.owner,
        "appID": info.app_id,
        "tenantID": info.tenant_id,
        "bundles": bundles,
        "status": common_status_to_json(info.status.as_ref()),
        "parentID": info.parent_id,
        "requestID": info.request_id,
        "traceID": info.trace_id,
        "opt": opt_json,
    })
}

fn local_scheduling_status_json(status: &str, message: &str) -> Value {
    json!({
        "status": status,
        "message": message,
    })
}

async fn update_local_scheduling_status(
    st: HttpState,
    q: LocalSchedulingStatusQuery,
    evicting: bool,
) -> Response {
    let Some(node_id) = q.node_id.map(|v| v.trim().to_string()).filter(|v| !v.is_empty()) else {
        return (
            StatusCode::BAD_REQUEST,
            Json(local_scheduling_status_json(
                "unknown",
                "node_id query parameter is required",
            )),
        )
            .into_response();
    };
    let status_label = if evicting { "evicting" } else { "normal" };
    match st
        .master
        .local_sched_mgr
        .update_scheduling_status(node_id.as_str(), evicting)
    {
        Ok(()) => (StatusCode::OK, Json(local_scheduling_status_json(status_label, "success")))
            .into_response(),
        Err(msg) if msg == "Invalid nodeID" => (
            StatusCode::NOT_FOUND,
            Json(local_scheduling_status_json(status_label, msg.as_str())),
        )
            .into_response(),
        Err(msg) => (
            StatusCode::SERVICE_UNAVAILABLE,
            Json(local_scheduling_status_json(status_label, msg.as_str())),
        )
            .into_response(),
    }
}

async fn mark_local_scheduler_evicting(
    State(st): State<HttpState>,
    Query(q): Query<LocalSchedulingStatusQuery>,
) -> Response {
    update_local_scheduling_status(st, q, true).await
}

async fn clear_local_scheduler_evicting(
    State(st): State<HttpState>,
    Query(q): Query<LocalSchedulingStatusQuery>,
) -> Response {
    update_local_scheduling_status(st, q, false).await
}

/// C++ resource_group_manager_driver.h: QueryRGroupHandler
async fn query_resource_group(
    State(st): State<HttpState>,
    headers: HeaderMap,
    body: Option<axum::body::Bytes>,
) -> Response {
    let use_json = parse_type_header(&headers);

    let (request_id, r_group_name) = if use_json {
        let q: ResourceGroupQuery = body
            .and_then(|b| serde_json::from_slice(&b).ok())
            .unwrap_or_default();
        (
            q.request_id.unwrap_or_else(|| uuid::Uuid::new_v4().to_string()),
            q.r_group_name.unwrap_or_default(),
        )
    } else {
        let req = body
            .and_then(|b| QueryResourceGroupRequest::decode(b.as_ref()).ok())
            .unwrap_or_default();
        (
            if req.request_id.is_empty() {
                uuid::Uuid::new_v4().to_string()
            } else {
                req.request_id
            },
            req.r_group_name,
        )
    };

    let mut groups = collect_resource_group_infos(
        st.master.instances.list_json().as_str(),
        r_group_name.as_str(),
        request_id.as_str(),
    );
    let metastore_groups =
        load_resource_group_infos_from_metastore(st.metastore.clone(), r_group_name.as_str()).await;
    if !metastore_groups.is_empty() {
        let mut merged = BTreeMap::new();
        for group in metastore_groups {
            merged.insert(resource_group_map_key(&group), group);
        }
        for group in groups {
            let key = resource_group_map_key(&group);
            if let Some(persisted) = merged.get_mut(&key) {
                *persisted = merge_resource_group_info(persisted.clone(), &group);
            } else {
                merged.insert(key, group);
            }
        }
        groups = merged.into_values().collect();
    }
    if use_json {
        let groups_json: Vec<Value> = groups.iter().map(resource_group_info_to_json).collect();
        Json(json!({
            "requestID": request_id,
            "rGroup": groups_json,
            "groups": groups_json,
            "count": groups.len(),
        }))
        .into_response()
    } else {
        let resp = QueryResourceGroupResponse {
            request_id,
            r_group: groups,
        };
        protobuf_http_response(resp.encode_to_vec())
    }
}

async fn resources(
    State(st): State<HttpState>,
    headers: HeaderMap,
) -> Result<Response, StatusCode> {
    let type_val = headers.get("Type").and_then(|v| v.to_str().ok());
    let use_json = match type_val {
        Some(TYPE_PROTOBUF) => false,
        Some(v) if !is_json_type(Some(v)) => return Err(StatusCode::BAD_REQUEST),
        _ => true,
    };

    let mut topo = st.master.topology.resource_summary_json();
    let inst: serde_json::Value =
        serde_json::from_str(&st.master.instances.list_json()).unwrap_or_else(|_| json!({}));
    if let Some(obj) = topo.as_object_mut() {
        obj.insert("instances_tracked".into(), json!(st.master.instances.count()));
        obj.insert("instances".into(), inst);
        obj.insert(
            "node_registry".into(),
            st.master.node_manager.summary_json(),
        );
    }

    if use_json {
        Ok(Json(topo).into_response())
    } else {
        let list_json = st.master.instances.list_json();
        let instance_infos: Vec<InstanceInfo> = collect_sorted_instance_infos(&list_json);
        let mut instances = HashMap::new();
        for info in instance_infos {
            let id = info.instance_id.clone();
            if !id.is_empty() {
                instances.insert(id, info);
            }
        }
        let resource = ResourceUnit {
            id: st.master.config.cluster_id.clone(),
            instances,
            ..Default::default()
        };
        let resp = ResourceInfo {
            request_id: String::new(),
            resource: Some(resource),
        };
        Ok(protobuf_http_response(resp.encode_to_vec()))
    }
}
