//! `InstanceManager` in-memory behavior.

use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::thread;

use serde_json::json;
use yr_master::instances::InstanceManager;
use yr_master::snapshot::SnapshotManager;

fn im() -> InstanceManager {
    InstanceManager::new(
        Arc::new(AtomicBool::new(true)),
        SnapshotManager::new(),
        None,
    )
}

#[test]
fn upsert_instance_increments_count() {
    let m = im();
    assert_eq!(m.count(), 0);
    m.upsert_instance("/pre/i1", json!({"id": "i1", "tenant": "a"}));
    assert_eq!(m.count(), 1);
}

#[test]
fn upsert_exited_persists_snapshot() {
    let snaps = SnapshotManager::new();
    let m = InstanceManager::new(Arc::new(AtomicBool::new(true)), snaps.clone(), None);
    m.upsert_instance(
        "/pre/e1",
        json!({
            "id": "e1",
            "function_name": "fn",
            "tenant": "ten",
            "state": "EXITED",
            "created_at_ms": 10,
            "updated_at_ms": 20,
            "node_id": "n1",
            "function_proxy_id": "p1",
        }),
    );
    let s = snaps.get("e1").expect("snapshot");
    assert_eq!(s.function_name, "fn");
    assert_eq!(s.tenant_id, "ten");
    assert_eq!(s.exit_time, 20);
}

#[test]
fn remove_instance_key_decrements_count() {
    let m = im();
    m.upsert_instance("/pre/i1", json!({"id": "i1"}));
    m.remove_instance_key("/pre/i1");
    assert_eq!(m.count(), 0);
}

#[test]
fn list_json_roundtrips_sorted_keys() {
    let m = im();
    m.upsert_instance("/x/b", json!({"id": "b"}));
    m.upsert_instance("/x/a", json!({"id": "a"}));
    let s = m.list_json();
    let pos_a = s.find("\"a\"").expect("key a");
    let pos_b = s.find("\"b\"").expect("key b");
    assert!(pos_a < pos_b, "keys should be sorted: {s}");
}

#[test]
fn query_by_tenant_filters_tenant_field() {
    let m = im();
    m.upsert_instance("/i1", json!({"id": "i1", "tenant": "t1"}));
    m.upsert_instance("/i2", json!({"id": "i2", "tenant": "t2"}));
    let (rows, n) = m.query_by_tenant("t1", None);
    assert_eq!(n, 1);
    assert_eq!(rows[0]["id"], "i1");
}

#[test]
fn query_by_tenant_with_instance_id_filters() {
    let m = im();
    m.upsert_instance("/i1", json!({"id": "i1", "tenant": "t1"}));
    m.upsert_instance("/i2", json!({"id": "i2", "tenant": "t1"}));
    let (rows, n) = m.query_by_tenant("t1", Some("i2"));
    assert_eq!(n, 1);
    assert_eq!(rows[0]["id"], "i2");
}

#[test]
fn query_named_instances_empty_without_designated() {
    let m = im();
    m.upsert_instance("/a", json!({"id": "a"}));
    let (rows, rid) = m.query_named_instances("rq");
    assert_eq!(rid, "rq");
    assert!(rows.is_empty());
}

#[test]
fn query_named_instances_requires_designated_instance_id() {
    let m = im();
    m.upsert_instance("/a", json!({"id": "a", "tenant": "t"}));
    m.upsert_instance(
        "/b",
        json!({"id": "b", "designated_instance_id": "dn"}),
    );
    let (rows, rid) = m.query_named_instances("r1");
    assert_eq!(rid, "r1");
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0]["id"], "b");
}

#[test]
fn query_debug_instances_includes_key_field() {
    let m = im();
    m.upsert_instance("/path/inst", json!({"id": "inst", "tenant": "x"}));
    let rows = m.query_debug_instances();
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0]["key"], "inst");
    assert_eq!(rows[0]["id"], "inst");
}

#[test]
fn count_matches_manual_inserts() {
    let m = im();
    for i in 0..5 {
        m.upsert_instance(&format!("/k/{i}"), json!({"id": format!("id{i}")}));
    }
    assert_eq!(m.count(), 5);
}

#[test]
fn generation_increments_on_metastore_reconnect() {
    let m = im();
    assert_eq!(m.generation(), 0);
    m.on_metastore_reconnect();
    assert_eq!(m.generation(), 1);
}

#[test]
fn concurrent_upserts_unique_ids_reach_expected_count() {
    let m = Arc::new(im());
    let mut handles = vec![];
    for t in 0..4 {
        let mm = m.clone();
        handles.push(thread::spawn(move || {
            for k in 0..50 {
                let id = t * 1000 + k;
                mm.upsert_instance(&format!("/p/{id}"), json!({"id": format!("{id}")}));
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
    assert_eq!(m.count(), 200);
}

#[test]
fn try_forward_or_kill_requires_leader() {
    let leader = Arc::new(AtomicBool::new(false));
    let m = InstanceManager::new(leader.clone(), SnapshotManager::new(), None);
    assert!(!m.try_forward_or_kill("x"));
    leader.store(true, std::sync::atomic::Ordering::SeqCst);
    assert!(m.try_forward_or_kill("x"));
}

#[test]
fn is_scheduler_abnormal_false_when_empty() {
    let m = im();
    assert!(!m.is_scheduler_abnormal("any-proxy"));
}

#[test]
fn empty_tenant_query_returns_empty() {
    let m = im();
    m.upsert_instance("/i", json!({"id": "i", "tenant": "only"}));
    let (rows, n) = m.query_by_tenant("missing", None);
    assert_eq!(n, 0);
    assert!(rows.is_empty());
}

#[test]
fn query_by_group_matches_group_id_field() {
    let m = im();
    m.upsert_instance(
        "/g/a",
        json!({"id": "a", "group_id": "G1"}),
    );
    m.upsert_instance("/g/b", json!({"id": "b", "groupID": "G1"}));
    m.upsert_instance("/g/c", json!({"id": "c", "group_id": "G2"}));
    let rows = m.query_by_group("G1");
    assert_eq!(rows.len(), 2);
}

#[test]
fn lifecycle_phase_maps_status_strings() {
    assert_eq!(
        InstanceManager::lifecycle_phase(&json!({"status": "Running"})),
        "running"
    );
    assert_eq!(
        InstanceManager::lifecycle_phase(&json!({"phase": "STOPPED"})),
        "terminal"
    );
}

#[test]
fn query_by_tenant_ignores_instances_without_tenant_field() {
    let m = im();
    m.upsert_instance("/i", json!({"id": "i"}));
    let (rows, n) = m.query_by_tenant("any", None);
    assert_eq!(n, 0);
    assert!(rows.is_empty());
}
