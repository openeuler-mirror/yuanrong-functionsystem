//! Contract tests for etcd logical key layout (no live etcd).
//! Aligns with C++ metastore key generators.

use yr_common::etcd_keys::{
    explorer, gen_aksk_key, gen_aksk_watch_prefix, gen_busproxy_node_key, gen_busproxy_node_prefix,
    gen_func_meta_key, gen_instance_key, gen_instance_route_key, gen_pod_pool_key, gen_token_key,
    gen_token_watch_prefix, with_prefix, BUSPROXY_PATH_PREFIX, FUNC_META_PATH_PREFIX,
    GROUP_PATH_PREFIX, GROUP_SCHEDULE, INSTANCE_PATH_PREFIX, INSTANCE_ROUTE_PATH_PREFIX,
    INTERNAL_IAM_AKSK_PREFIX, INTERNAL_IAM_TOKEN_PREFIX, POD_POOL_PREFIX, SCHEDULER_TOPOLOGY,
};

#[test]
fn with_prefix_empty_leaves_key_unchanged() {
    assert_eq!(with_prefix("", SCHEDULER_TOPOLOGY), SCHEDULER_TOPOLOGY);
}

#[test]
fn with_prefix_concatenates_without_extra_separator() {
    assert_eq!(
        with_prefix("/tbl", SCHEDULER_TOPOLOGY),
        "/tbl/scheduler/topology"
    );
}

#[test]
fn gen_instance_key_requires_exactly_three_function_segments() {
    assert!(gen_instance_key("a/b", "i", "r").is_none());
    assert!(gen_instance_key("a/b/c/d", "i", "r").is_none());
    assert!(gen_instance_key("a/b/c", "i", "r").is_some());
}

#[test]
fn gen_instance_key_embeds_tenant_function_version_request_instance() {
    let k = gen_instance_key("t1/fn/v1", "inst-x", "req-y").unwrap();
    assert_eq!(
        k,
        "/sn/instance/business/yrk/tenant/t1/function/fn/version/v1/defaultaz/req-y/inst-x"
    );
}

#[test]
fn gen_instance_key_unique_per_instance_id() {
    let a = gen_instance_key("t/f/v", "i1", "r").unwrap();
    let b = gen_instance_key("t/f/v", "i2", "r").unwrap();
    assert_ne!(a, b);
}

#[test]
fn gen_instance_key_unique_per_request_id() {
    let a = gen_instance_key("t/f/v", "i", "r1").unwrap();
    let b = gen_instance_key("t/f/v", "i", "r2").unwrap();
    assert_ne!(a, b);
}

#[test]
fn gen_instance_route_key_is_instance_route_prefix_plus_id() {
    let id = "abc-123";
    let key = gen_instance_route_key(id);
    assert_eq!(key, format!("{}/{}", INSTANCE_ROUTE_PATH_PREFIX, id));
    assert!(key.starts_with(INSTANCE_ROUTE_PATH_PREFIX));
}

#[test]
fn gen_busproxy_node_key_matches_documented_shape() {
    assert_eq!(
        gen_busproxy_node_key("tenantSeg", "node-7"),
        format!("{}/tenantSeg/node/node-7", BUSPROXY_PATH_PREFIX)
    );
}

#[test]
fn gen_busproxy_node_prefix_contains_every_node_key_for_tenant() {
    let seg = "0";
    let prefix = gen_busproxy_node_prefix(seg);
    let k1 = gen_busproxy_node_key(seg, "a");
    let k2 = gen_busproxy_node_key(seg, "b");
    assert!(prefix.ends_with("/node/"));
    assert!(k1.starts_with(&prefix));
    assert!(k2.starts_with(&prefix));
}

#[test]
fn gen_busproxy_distinct_tenants_have_non_overlapping_prefixes_by_segment() {
    let p0 = gen_busproxy_node_prefix("0");
    let p1 = gen_busproxy_node_prefix("1");
    assert_ne!(p0, p1);
    assert!(!p0.starts_with(&p1));
    assert!(!p1.starts_with(&p0));
}

#[test]
fn gen_pod_pool_key_is_prefix_plus_pool_id() {
    assert_eq!(
        gen_pod_pool_key("pool-9"),
        format!("{}/pool-9", POD_POOL_PREFIX)
    );
}

#[test]
fn gen_token_key_new_uses_new_infix() {
    assert_eq!(
        gen_token_key("cl", "tn", true),
        format!("{}{}/cl/tn", INTERNAL_IAM_TOKEN_PREFIX, yr_common::etcd_keys::NEW_INFIX)
    );
}

#[test]
fn gen_token_key_old_uses_old_infix() {
    assert_eq!(
        gen_token_key("cl", "tn", false),
        format!("{}{}/cl/tn", INTERNAL_IAM_TOKEN_PREFIX, yr_common::etcd_keys::OLD_INFIX)
    );
}

#[test]
fn gen_token_watch_prefix_is_prefix_of_full_token_keys_for_same_cluster() {
    let wp_new = gen_token_watch_prefix("c1", true);
    let wp_old = gen_token_watch_prefix("c1", false);
    assert_eq!(wp_new, format!("{}{}/c1", INTERNAL_IAM_TOKEN_PREFIX, yr_common::etcd_keys::NEW_INFIX));
    assert_eq!(wp_old, format!("{}{}/c1", INTERNAL_IAM_TOKEN_PREFIX, yr_common::etcd_keys::OLD_INFIX));
    let full = gen_token_key("c1", "t9", true);
    assert!(full.starts_with(&(wp_new + "/")));
}

#[test]
fn gen_aksk_key_new_and_old_differ_only_by_infix() {
    let n = gen_aksk_key("c", "t", true);
    let o = gen_aksk_key("c", "t", false);
    assert_ne!(n, o);
    assert!(n.contains("/new/"));
    assert!(o.contains("/old/"));
    assert!(n.starts_with(INTERNAL_IAM_AKSK_PREFIX));
    assert!(o.starts_with(INTERNAL_IAM_AKSK_PREFIX));
}

#[test]
fn gen_aksk_watch_prefix_clusters_tokens_for_cluster() {
    let wp = gen_aksk_watch_prefix("clusterZ", true);
    assert_eq!(
        wp,
        format!("{}{}/clusterZ", INTERNAL_IAM_AKSK_PREFIX, yr_common::etcd_keys::NEW_INFIX)
    );
    let key = gen_aksk_key("clusterZ", "tenantA", true);
    assert!(key.starts_with(&(wp + "/")));
}

#[test]
fn gen_aksk_distinct_tenants_under_same_cluster() {
    let a = gen_aksk_key("c", "t1", true);
    let b = gen_aksk_key("c", "t2", true);
    assert_ne!(a, b);
}

#[test]
fn gen_func_meta_key_three_part_function_key() {
    let k = gen_func_meta_key("mytenant/myfn/1.0").unwrap();
    assert_eq!(
        k,
        format!(
            "{}/mytenant/function/myfn/version/1.0",
            FUNC_META_PATH_PREFIX
        )
    );
}

#[test]
fn gen_func_meta_key_rejects_bad_segment_count() {
    assert!(gen_func_meta_key("only/two").is_none());
}

#[test]
fn instance_path_prefix_constant_shape() {
    assert!(INSTANCE_PATH_PREFIX.starts_with("/sn/instance/"));
}

#[test]
fn group_schedule_matches_group_path_prefix_segment() {
    assert!(GROUP_SCHEDULE.starts_with(GROUP_PATH_PREFIX));
}

#[test]
fn explorer_master_election_keys_are_absolute_paths() {
    assert!(explorer::DEFAULT_MASTER_ELECTION_KEY.starts_with("/yr/leader/"));
    assert!(explorer::IAM_SERVER_MASTER_ELECTION_KEY.starts_with("/yr/leader/"));
}
