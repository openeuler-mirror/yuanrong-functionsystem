//! Registration / heartbeat protobuf payloads.

use serde_json::Value;
use yr_agent::registration::{
    build_global_register_request, build_resource_update_request, heartbeat_evict_request,
};

#[test]
fn heartbeat_evict_has_empty_list_and_fixed_reason() {
    let r = heartbeat_evict_request();
    assert!(r.instance_ids.is_empty());
    assert_eq!(r.reason, "yr-agent-heartbeat");
}

#[test]
fn heartbeat_reason_is_stable_literal() {
    let r = heartbeat_evict_request();
    assert!(
        r.reason.contains("heartbeat"),
        "reason should identify heartbeat traffic"
    );
}

#[test]
fn global_register_request_matches_expected_shape() {
    let r = build_global_register_request("agent-7", "http://192.168.1.5:22799");
    assert_eq!(r.node_id, "agent-7");
    assert_eq!(r.address, "http://192.168.1.5:22799");
    assert_eq!(r.resource_json, "{}");
    let v: Value = serde_json::from_str(&r.agent_info_json).unwrap();
    assert_eq!(v["role"], "function_agent");
    assert_eq!(v["node_id"], "agent-7");
    assert_eq!(v["grpc"], "http://192.168.1.5:22799");
}

#[test]
fn global_register_agent_info_is_valid_json_object() {
    let r = build_global_register_request("n", "http://h:1");
    let v: Value = serde_json::from_str(&r.agent_info_json).expect("agent_info_json parses");
    assert!(v.is_object());
    assert_eq!(v.as_object().unwrap().len(), 3);
}

#[test]
fn resource_update_request_carries_node_and_json_blob() {
    let json = r#"{"i1":{"runtime_id":"r1","status":"running","exit_code":0}}"#;
    let r = build_resource_update_request("node-42", json.to_string());
    assert_eq!(r.node_id, "node-42");
    assert_eq!(r.resource_json, json);
}
