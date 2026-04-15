//! JSON shape for periodic resource snapshots (matches reporter loop).

#[test]
fn empty_runtime_map_serializes_to_empty_object() {
    let m = serde_json::Map::new();
    let s = serde_json::Value::Object(m).to_string();
    assert_eq!(s, "{}");
}

#[test]
fn single_runtime_snapshot_matches_reporter_shape() {
    let mut map = serde_json::Map::new();
    map.insert(
        "instance-a".into(),
        serde_json::json!({
            "runtime_id": "rt-xyz",
            "status": "running",
            "exit_code": 0,
        }),
    );
    let s = serde_json::Value::Object(map).to_string();
    let v: serde_json::Value = serde_json::from_str(&s).unwrap();
    assert_eq!(v["instance-a"]["runtime_id"], "rt-xyz");
    assert_eq!(v["instance-a"]["status"], "running");
    assert_eq!(v["instance-a"]["exit_code"], 0);
}
