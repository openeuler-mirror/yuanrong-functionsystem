//! UUID, timestamps, parsing helpers, path/key utilities, and small constant checks.

use std::collections::HashSet;
use std::str::FromStr;
use std::time::{SystemTime, UNIX_EPOCH};

use yr_common::config::load_config_from_json_str;
use yr_common::constants::{actor_name, register, scalars, signal};
use yr_common::etcd_keys::{
    gen_aksk_watch_prefix, gen_busproxy_node_key, gen_func_meta_key, gen_instance_key,
    gen_instance_route_key, gen_pod_pool_key, gen_token_watch_prefix, with_prefix,
};
use yr_common::heartbeat::HeartbeatPeer;
use yr_common::types::InstanceState;

#[test]
fn uuid_v4_uniqueness_in_batch() {
    let mut set = HashSet::new();
    for _ in 0..256 {
        let u = uuid::Uuid::new_v4();
        assert!(set.insert(u));
    }
}

#[test]
fn uuid_v4_version_nibble() {
    let u = uuid::Uuid::new_v4();
    assert_eq!(u.get_version_num(), 4);
}

#[test]
fn system_time_since_epoch_is_monotonic_non_decreasing() {
    let a = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    let b = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    assert!(b >= a);
}

#[test]
fn format_timestamp_secs_decimal() {
    let secs = 1_700_000_000u64;
    let s = format!("{secs}");
    assert_eq!(s, "1700000000");
}

#[test]
fn parse_u64_from_str_ok() {
    assert_eq!("42".parse::<u64>().unwrap(), 42);
}

#[test]
fn parse_i32_negative_ok() {
    assert_eq!("-7".parse::<i32>().unwrap(), -7);
}

#[test]
fn parse_bool_true_false() {
    assert!("true".parse::<bool>().unwrap());
    assert!(!"false".parse::<bool>().unwrap());
}

#[test]
fn trim_path_like_string() {
    let s = "  /a/b/  ";
    assert_eq!(s.trim(), "/a/b/");
}

#[test]
fn join_path_segments_manual() {
    let base = "/yr";
    let tail = "x/y";
    assert_eq!(format!("{}/{}", base.trim_end_matches('/'), tail), "/yr/x/y");
}

#[test]
fn with_prefix_empty_is_identity() {
    assert_eq!(with_prefix("", "/k"), "/k");
}

#[test]
fn with_prefix_concat_no_separator() {
    assert_eq!(with_prefix("pre", "fix"), "prefix");
}

#[test]
fn gen_instance_route_key_shape() {
    assert_eq!(
        gen_instance_route_key("i-1"),
        "/yr/route/business/yrk/i-1"
    );
}

#[test]
fn gen_pod_pool_key_shape() {
    assert_eq!(gen_pod_pool_key("pool-a"), "/yr/podpools/info/pool-a");
}

#[test]
fn gen_busproxy_node_key_contains_tenant_and_node() {
    let k = gen_busproxy_node_key("t9", "n1");
    assert!(k.contains("t9"));
    assert!(k.ends_with("/n1"));
}

#[test]
fn gen_token_watch_prefix_new_vs_old() {
    assert_ne!(
        gen_token_watch_prefix("c", true),
        gen_token_watch_prefix("c", false)
    );
}

#[test]
fn gen_aksk_watch_prefix_contains_cluster() {
    let p = gen_aksk_watch_prefix("clu", true);
    assert!(p.contains("clu"));
}

#[test]
fn gen_instance_key_three_part_ok() {
    assert!(gen_instance_key("a/b/c", "i", "r").is_some());
}

#[test]
fn gen_instance_key_wrong_part_count_none() {
    assert!(gen_instance_key("a/b", "i", "r").is_none());
}

#[test]
fn gen_func_meta_key_three_part_ok() {
    assert!(gen_func_meta_key("t/f/v").is_some());
}

#[test]
fn heartbeat_peer_client_aid_contains_suffix() {
    let n = HeartbeatPeer::client_aid_name("comp-", "dst");
    assert!(n.contains("HeartbeatClient-"));
    assert!(n.ends_with(register::REGISTER_HELPER_SUFFIX));
}

#[test]
fn heartbeat_peer_observer_aid_prefix() {
    let n = HeartbeatPeer::observer_aid_name("hb1");
    assert!(n.starts_with("HeartbeatObserver-"));
    assert!(n.ends_with("hb1"));
}

#[test]
fn signal_to_string_known() {
    assert_eq!(
        signal::signal_to_string(signal::SHUT_DOWN_SIGNAL),
        "SHUT_DOWN_SIGNAL"
    );
}

#[test]
fn signal_to_string_unknown() {
    assert_eq!(signal::signal_to_string(9999), "UnknownSignal");
}

#[test]
fn signal_bounds_constants() {
    assert!(signal::MIN_SIGNAL_NUM < signal::MAX_SIGNAL_NUM);
    assert!(signal::MIN_USER_SIGNAL_NUM >= signal::MIN_SIGNAL_NUM);
}

#[test]
fn actor_name_request_router_non_empty() {
    assert!(!actor_name::REQUEST_ROUTER_NAME.is_empty());
}

#[test]
fn scalars_default_system_timeout_positive() {
    assert!(scalars::DEFAULT_SYSTEM_TIMEOUT > 0);
}

#[test]
fn register_default_timeout_ms_positive() {
    assert!(register::DEFAULT_REGISTER_TIMEOUT_MS > 0);
}

#[test]
fn instance_state_fromstr_display_consistent() {
    let st = InstanceState::Suspend;
    assert_eq!(
        InstanceState::from_str(&st.to_string()).unwrap(),
        st
    );
}

#[test]
fn load_config_json_str_rejects_garbage() {
    let r: Result<serde_json::Value, _> = load_config_from_json_str("%%%");
    assert!(r.is_err());
}
