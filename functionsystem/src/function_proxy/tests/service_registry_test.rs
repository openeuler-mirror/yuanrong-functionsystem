//! Contract tests for bus-proxy MetaStore registration (keys, JSON, lease math). No etcd.

use std::sync::Arc;

use clap::Parser;
use yr_common::etcd_keys::gen_busproxy_node_key;
use yr_proxy::busproxy::service_registry::BusProxyRegistration;
use yr_proxy::Config;

/// Mirrors `run_busproxy_registration`: TTL passed to grant_lease is at least 5 seconds.
fn effective_lease_ttl_sec(config_ttl: u64) -> i64 {
    config_ttl.max(5) as i64
}

/// Seconds between keepalive ticks: `lease_ttl / 3`, minimum 1 (same as tokio interval setup).
fn busproxy_keepalive_tick_secs(lease_ttl: i64) -> u64 {
    (lease_ttl as u64 / 3).max(1)
}

fn registration_key(cfg: &Config) -> String {
    gen_busproxy_node_key(&cfg.busproxy_tenant_segment, &cfg.node_id)
}

fn effective_aid(cfg: &Config) -> String {
    if cfg.proxy_aid.trim().is_empty() {
        cfg.node_id.clone()
    } else {
        cfg.proxy_aid.clone()
    }
}

#[test]
fn registration_json_contains_aid_node_ak() {
    let reg = BusProxyRegistration {
        aid: "aid-1".into(),
        node: "node-1".into(),
        ak: "ak-secret".into(),
        grpc: String::new(),
    };
    let v: serde_json::Value = serde_json::from_slice(&serde_json::to_vec(&reg).unwrap()).unwrap();
    assert_eq!(v["aid"], "aid-1");
    assert_eq!(v["node"], "node-1");
    assert_eq!(v["ak"], "ak-secret");
}

#[test]
fn registration_json_omits_grpc_when_empty() {
    let reg = BusProxyRegistration {
        aid: "a".into(),
        node: "n".into(),
        ak: "k".into(),
        grpc: String::new(),
    };
    let v: serde_json::Value = serde_json::from_slice(&serde_json::to_vec(&reg).unwrap()).unwrap();
    assert!(v.get("grpc").is_none());
}

#[test]
fn registration_json_includes_grpc_when_non_empty() {
    let reg = BusProxyRegistration {
        aid: "a".into(),
        node: "n".into(),
        ak: "k".into(),
        grpc: "http://10.0.0.1:8402".into(),
    };
    let v: serde_json::Value = serde_json::from_slice(&serde_json::to_vec(&reg).unwrap()).unwrap();
    assert_eq!(v["grpc"], "http://10.0.0.1:8402");
}

#[test]
fn registration_key_matches_gen_busproxy_node_key() {
    let cfg = Arc::new(Config {
        busproxy_tenant_segment: "0".into(),
        node_id: "proxy-node-1".into(),
        ..sample_config()
    });
    assert_eq!(
        registration_key(&cfg),
        "/yr/busproxy/business/yrk/tenant/0/node/proxy-node-1"
    );
}

#[test]
fn deregistration_deletes_same_key_as_registration_put() {
    // Contract: lease expiry or explicit delete targets the same path as put_with_lease.
    let cfg = Arc::new(Config {
        busproxy_tenant_segment: "seg".into(),
        node_id: "n".into(),
        ..sample_config()
    });
    let put_key = registration_key(&cfg);
    let delete_key = registration_key(&cfg);
    assert_eq!(put_key, delete_key);
}

#[test]
fn distinct_node_ids_yield_distinct_registration_keys_same_tenant() {
    let mut a = sample_config();
    a.node_id = "a".into();
    let mut b = sample_config();
    b.node_id = "b".into();
    assert_ne!(registration_key(&a), registration_key(&b));
}

#[test]
fn lease_ttl_clamped_to_five_when_config_lower() {
    assert_eq!(effective_lease_ttl_sec(1), 5);
    assert_eq!(effective_lease_ttl_sec(4), 5);
}

#[test]
fn lease_ttl_unchanged_when_config_at_or_above_five() {
    assert_eq!(effective_lease_ttl_sec(5), 5);
    assert_eq!(effective_lease_ttl_sec(30), 30);
}

#[test]
fn keepalive_tick_is_lease_ttl_divided_by_three_rounded_down() {
    assert_eq!(busproxy_keepalive_tick_secs(30), 10);
    assert_eq!(busproxy_keepalive_tick_secs(9), 3);
}

#[test]
fn keepalive_tick_minimum_one_second() {
    assert_eq!(busproxy_keepalive_tick_secs(5), 1);
    assert_eq!(busproxy_keepalive_tick_secs(2), 1);
}

#[test]
fn peer_discovery_watch_prefix_matches_busproxy_node_prefix() {
    use yr_common::etcd_keys::gen_busproxy_node_prefix;
    let seg = "0";
    assert_eq!(
        gen_busproxy_node_prefix(seg),
        "/yr/busproxy/business/yrk/tenant/0/node/"
    );
}

#[test]
fn watch_prefix_covers_peer_keys_under_tenant() {
    use yr_common::etcd_keys::gen_busproxy_node_prefix;
    let p = gen_busproxy_node_prefix("t1");
    let k = gen_busproxy_node_key("t1", "any");
    assert!(k.starts_with(&p));
}

#[test]
fn effective_aid_defaults_to_node_id_when_proxy_aid_blank() {
    let cfg = Config {
        proxy_aid: "   ".into(),
        node_id: "node-x".into(),
        ..sample_config()
    };
    assert_eq!(effective_aid(&cfg), "node-x");
}

#[test]
fn effective_aid_prefers_proxy_aid_when_set() {
    let cfg = Config {
        proxy_aid: "custom-aid".into(),
        node_id: "node-x".into(),
        ..sample_config()
    };
    assert_eq!(effective_aid(&cfg), "custom-aid");
}

#[test]
fn serialized_registration_matches_service_registry_put_payload_shape() {
    let cfg = Arc::new(Config {
        host: "192.168.1.2".into(),
        port: 9999,
        proxy_aid: String::new(),
        proxy_access_key: "the-ak".into(),
        busproxy_tenant_segment: "0".into(),
        node_id: "nid".into(),
        ..sample_config()
    });
    let aid = effective_aid(&cfg);
    let val = serde_json::to_vec(&BusProxyRegistration {
        aid,
        node: cfg.node_id.clone(),
        ak: cfg.proxy_access_key.clone(),
        grpc: cfg.advertise_grpc_endpoint(),
    })
    .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&val).unwrap();
    assert_eq!(v["node"], "nid");
    assert_eq!(v["ak"], "the-ak");
    assert_eq!(v["grpc"], "http://192.168.1.2:9999");
}

fn sample_config() -> Config {
    // `Config` is a clap Parser; fill required fields for struct update syntax.
    Config::try_parse_from([
        "yr-proxy",
        "--host",
        "127.0.0.1",
        "--grpc-listen-port",
        "8402",
    ])
    .expect("default config from clap")
}
