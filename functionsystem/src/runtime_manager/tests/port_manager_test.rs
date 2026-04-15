//! Port pool allocation (in-process).

use yr_runtime_manager::port_manager::{PortManager, SharedPortManager};

#[test]
fn allocate_and_release_round_trip() {
    let mut pm = PortManager::new(40_000, 4).unwrap();
    let p1 = pm.allocate("rt-a").unwrap();
    let p2 = pm.allocate("rt-b").unwrap();
    assert_ne!(p1, p2);
    assert_eq!(pm.port_for("rt-a"), Some(p1));
    assert_eq!(pm.release("rt-a"), Some(p1));
    assert_eq!(pm.port_for("rt-a"), None);
    let p3 = pm.allocate("rt-c").unwrap();
    assert!(pm.port_for("rt-b") == Some(p2));
    assert!(pm.port_for("rt-c") == Some(p3));
}

#[test]
fn exhaustion_errors() {
    let mut pm = PortManager::new(50_000, 2).unwrap();
    pm.allocate("a").unwrap();
    pm.allocate("b").unwrap();
    let err = pm.allocate("c").unwrap_err();
    assert!(err.to_string().contains("no free ports") || err.to_string().contains("50"));
}

#[tokio::test]
async fn shared_port_manager_thread_safe_use() {
    let pm = SharedPortManager::new(60_000, 3).unwrap();
    let p = pm.allocate("worker-1").unwrap();
    assert!(p >= 60_000 && p < 60_003);
    assert_eq!(pm.release("worker-1"), Some(p));
}

#[test]
fn new_rejects_zero_port_count() {
    let r = PortManager::new(10_000, 0);
    assert!(r.is_err());
    let err = r.err().unwrap();
    assert!(err.to_string().contains("port_count"));
}

#[test]
fn new_rejects_range_overflow_past_u16_max() {
    let r = PortManager::new(65530, 20);
    assert!(r.is_err());
    let err = r.err().unwrap();
    assert!(err.to_string().to_lowercase().contains("overflow"));
}

#[test]
fn shared_rejects_second_allocate_for_same_runtime_id() {
    let pm = SharedPortManager::new(61_000, 5).unwrap();
    pm.allocate("same-id").unwrap();
    let err = pm.allocate("same-id").unwrap_err();
    assert!(err.to_string().contains("already has a port"));
}
