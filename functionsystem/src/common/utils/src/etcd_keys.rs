/// Etcd key constants and generators, aligned 1:1 with C++ `metastore_keys.h`,
/// `meta_store_kv_operation.h`, and `explorer_actor.h`.
///
/// All logical keys match the C++ values exactly. Physical etcd keys = etcd_table_prefix + logical key.
/// When prefix is empty, logical key IS the physical key (no extra separator added).

// --- metastore_keys.h ---
pub const SCHEDULER_TOPOLOGY: &str = "/scheduler/topology";
/// Logical base for domain scheduler registration keys (under `etcd_table_prefix`).
pub const YR_DOMAIN_SCHEDULER_PREFIX: &str = "/yr/domain-scheduler";
/// Logical base segment for global scheduler / master topology (`{prefix}{YR_MASTER_PREFIX}/...`).
pub const YR_MASTER_PREFIX: &str = "/yr/master";
pub const GROUP_SCHEDULE: &str = "/yr/group";
pub const READY_AGENT_CNT_KEY: &str = "/yr/readyAgentCount";

// --- meta_store_kv_operation.h ---
pub const INSTANCE_PATH_PREFIX: &str = "/sn/instance/business/yrk/tenant";
pub const GROUP_PATH_PREFIX: &str = "/yr/group";
pub const INSTANCE_ROUTE_PATH_PREFIX: &str = "/yr/route/business/yrk";
pub const BUSPROXY_PATH_PREFIX: &str = "/yr/busproxy/business/yrk/tenant";
pub const FUNC_META_PATH_PREFIX: &str = "/yr/functions/business/yrk/tenant";
/// Abnormal local scheduler markers (`instance_manager_actor.cpp` `KEY_ABNORMAL_SCHEDULER_PREFIX`).
pub const ABNORMAL_SCHEDULER_PREFIX: &str = "/yr/abnormal/localscheduler/";
pub const POD_POOL_PREFIX: &str = "/yr/podpools/info";
pub const INTERNAL_IAM_TOKEN_PREFIX: &str = "/yr/iam/token";
pub const INTERNAL_IAM_AKSK_PREFIX: &str = "/yr/iam/aksk";
/// Logical user records for IAM HTTP API (`/v1/users`).
pub const INTERNAL_IAM_USER_PREFIX: &str = "/yr/iam/users";
/// Logical tenant records for IAM HTTP API (`/v1/tenants`).
pub const INTERNAL_IAM_TENANT_PREFIX: &str = "/yr/iam/tenants";
pub const DEBUG_INSTANCE_PREFIX: &str = "/yr/debug/";
pub const NEW_INFIX: &str = "/new";
pub const OLD_INFIX: &str = "/old";

pub const INSTANCE_INFO_KEY_LEN: usize = 14;
pub const ROUTE_INFO_KEY_LEN: usize = 6;

pub mod explorer {
    pub const DEFAULT_MASTER_ELECTION_KEY: &str = "/yr/leader/function-master";
    pub const FUNCTION_MASTER_K8S_LEASE_NAME: &str = "function-master";
    pub const IAM_SERVER_MASTER_ELECTION_KEY: &str = "/yr/leader/function-iam";
    pub const IAM_SERVER_K8S_LEASE_NAME: &str = "function-iam";
}

/// Apply etcd table prefix to a logical key.
/// Mirrors C++ `KvClientStrategy::GetKeyWithPrefix`: concatenation, no extra separator.
pub fn with_prefix(prefix: &str, key: &str) -> String {
    if prefix.is_empty() {
        key.to_string()
    } else {
        format!("{}{}", prefix, key)
    }
}

/// Generate instance etcd key.
/// C++ `GenInstanceKey(function_key, instance_id, request_id)`.
/// `function_key` = "tenant/function_segment/version" (3 segments split by "/").
pub fn gen_instance_key(function_key: &str, instance_id: &str, request_id: &str) -> Option<String> {
    let parts: Vec<&str> = function_key.split('/').collect();
    if parts.len() != 3 {
        return None;
    }
    let (tenant, func_seg, version) = (parts[0], parts[1], parts[2]);
    Some(format!(
        "{}/{}/function/{}/version/{}/defaultaz/{}/{}",
        INSTANCE_PATH_PREFIX, tenant, func_seg, version, request_id, instance_id
    ))
}

/// Generate instance route key.
/// C++ `GenInstanceRouteKey(instance_id)`.
pub fn gen_instance_route_key(instance_id: &str) -> String {
    format!("{}/{}", INSTANCE_ROUTE_PATH_PREFIX, instance_id)
}

/// Bus-proxy registration key: `{BUSPROXY_PATH_PREFIX}/{tenant_segment}/node/{node_id}`.
/// Value is JSON: `{ "aid", "node", "ak" }` (aligned with C++ function_proxy).
pub fn gen_busproxy_node_key(tenant_segment: &str, node_id: &str) -> String {
    format!("{}/{}/node/{}", BUSPROXY_PATH_PREFIX, tenant_segment, node_id)
}

/// Prefix watch for all bus-proxy registrations under a tenant segment.
pub fn gen_busproxy_node_prefix(tenant_segment: &str) -> String {
    format!("{}/{}/node/", BUSPROXY_PATH_PREFIX, tenant_segment)
}

/// Generate pod pool key.
/// C++ `GenPodPoolKey(pool_id)`.
pub fn gen_pod_pool_key(pool_id: &str) -> String {
    format!("{}/{}", POD_POOL_PREFIX, pool_id)
}

/// Generate IAM token key.
/// C++ `GenTokenKey(cluster_id, tenant_id, is_new)`.
pub fn gen_token_key(cluster_id: &str, tenant_id: &str, is_new: bool) -> String {
    let infix = if is_new { NEW_INFIX } else { OLD_INFIX };
    format!("{}{}/{}/{}", INTERNAL_IAM_TOKEN_PREFIX, infix, cluster_id, tenant_id)
}

/// Generate IAM token watch prefix.
/// C++ `GenTokenKeyWatchPrefix(cluster_id, is_new)`.
pub fn gen_token_watch_prefix(cluster_id: &str, is_new: bool) -> String {
    let infix = if is_new { NEW_INFIX } else { OLD_INFIX };
    format!("{}{}/{}", INTERNAL_IAM_TOKEN_PREFIX, infix, cluster_id)
}

/// Generate IAM AK/SK key.
/// C++ `GenAKSKKey(cluster_id, tenant_id, is_new)`.
pub fn gen_aksk_key(cluster_id: &str, tenant_id: &str, is_new: bool) -> String {
    let infix = if is_new { NEW_INFIX } else { OLD_INFIX };
    format!("{}{}/{}/{}", INTERNAL_IAM_AKSK_PREFIX, infix, cluster_id, tenant_id)
}

/// Generate IAM AK/SK watch prefix.
pub fn gen_aksk_watch_prefix(cluster_id: &str, is_new: bool) -> String {
    let infix = if is_new { NEW_INFIX } else { OLD_INFIX };
    format!("{}{}/{}", INTERNAL_IAM_AKSK_PREFIX, infix, cluster_id)
}

/// `{INTERNAL_IAM_USER_PREFIX}/{cluster_id}/{user_id}`
pub fn gen_iam_user_key(cluster_id: &str, user_id: &str) -> String {
    format!("{}/{}/{}", INTERNAL_IAM_USER_PREFIX, cluster_id, user_id)
}

/// Prefix listing users for a cluster.
pub fn gen_iam_user_prefix(cluster_id: &str) -> String {
    format!("{}/{}/", INTERNAL_IAM_USER_PREFIX, cluster_id)
}

/// `{INTERNAL_IAM_TENANT_PREFIX}/{cluster_id}/{tenant_id}`
pub fn gen_iam_tenant_key(cluster_id: &str, tenant_id: &str) -> String {
    format!("{}/{}/{}", INTERNAL_IAM_TENANT_PREFIX, cluster_id, tenant_id)
}

pub fn gen_iam_tenant_prefix(cluster_id: &str) -> String {
    format!("{}/{}/", INTERNAL_IAM_TENANT_PREFIX, cluster_id)
}

/// Generate full function metadata key.
/// C++ `GenEtcdFullFuncKey(key)` where key = "tenant/function_name/version".
pub fn gen_func_meta_key(function_key: &str) -> Option<String> {
    let parts: Vec<&str> = function_key.split('/').collect();
    if parts.len() != 3 {
        return None;
    }
    let (tenant, func_name, version) = (parts[0], parts[1], parts[2]);
    Some(format!(
        "{}/{}/function/{}/version/{}",
        FUNC_META_PATH_PREFIX, tenant, func_name, version
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn constants_match_cpp() {
        assert_eq!(SCHEDULER_TOPOLOGY, "/scheduler/topology");
        assert_eq!(YR_DOMAIN_SCHEDULER_PREFIX, "/yr/domain-scheduler");
        assert_eq!(YR_MASTER_PREFIX, "/yr/master");
        assert_eq!(GROUP_SCHEDULE, "/yr/group");
        assert_eq!(READY_AGENT_CNT_KEY, "/yr/readyAgentCount");
        assert_eq!(INSTANCE_PATH_PREFIX, "/sn/instance/business/yrk/tenant");
        assert_eq!(INSTANCE_ROUTE_PATH_PREFIX, "/yr/route/business/yrk");
        assert_eq!(BUSPROXY_PATH_PREFIX, "/yr/busproxy/business/yrk/tenant");
        assert_eq!(FUNC_META_PATH_PREFIX, "/yr/functions/business/yrk/tenant");
        assert_eq!(ABNORMAL_SCHEDULER_PREFIX, "/yr/abnormal/localscheduler/");
        assert_eq!(POD_POOL_PREFIX, "/yr/podpools/info");
        assert_eq!(INTERNAL_IAM_TOKEN_PREFIX, "/yr/iam/token");
        assert_eq!(INTERNAL_IAM_AKSK_PREFIX, "/yr/iam/aksk");
        assert_eq!(INTERNAL_IAM_USER_PREFIX, "/yr/iam/users");
        assert_eq!(INTERNAL_IAM_TENANT_PREFIX, "/yr/iam/tenants");
        assert_eq!(DEBUG_INSTANCE_PREFIX, "/yr/debug/");

        assert_eq!(explorer::DEFAULT_MASTER_ELECTION_KEY, "/yr/leader/function-master");
        assert_eq!(explorer::IAM_SERVER_MASTER_ELECTION_KEY, "/yr/leader/function-iam");
        assert_eq!(explorer::FUNCTION_MASTER_K8S_LEASE_NAME, "function-master");
        assert_eq!(explorer::IAM_SERVER_K8S_LEASE_NAME, "function-iam");
    }

    #[test]
    fn with_prefix_empty() {
        assert_eq!(with_prefix("", "/scheduler/topology"), "/scheduler/topology");
    }

    #[test]
    fn with_prefix_nonempty() {
        assert_eq!(with_prefix("/myprefix", "/scheduler/topology"), "/myprefix/scheduler/topology");
    }

    #[test]
    fn gen_instance_key_matches_cpp() {
        let key = gen_instance_key("default/0-test-hello/$latest", "inst-001", "req-001");
        assert_eq!(
            key.unwrap(),
            "/sn/instance/business/yrk/tenant/default/function/0-test-hello/version/$latest/defaultaz/req-001/inst-001"
        );
    }

    #[test]
    fn gen_instance_key_bad_input() {
        assert!(gen_instance_key("only-two/parts", "a", "b").is_none());
    }

    #[test]
    fn gen_instance_route_key_matches_cpp() {
        assert_eq!(gen_instance_route_key("inst-001"), "/yr/route/business/yrk/inst-001");
    }

    #[test]
    fn gen_busproxy_node_key_shape() {
        assert_eq!(
            gen_busproxy_node_key("0", "node-a"),
            "/yr/busproxy/business/yrk/tenant/0/node/node-a"
        );
        assert_eq!(
            gen_busproxy_node_prefix("0"),
            "/yr/busproxy/business/yrk/tenant/0/node/"
        );
    }

    #[test]
    fn gen_token_key_new() {
        assert_eq!(gen_token_key("cluster1", "tenant1", true), "/yr/iam/token/new/cluster1/tenant1");
    }

    #[test]
    fn gen_token_key_old() {
        assert_eq!(gen_token_key("cluster1", "tenant1", false), "/yr/iam/token/old/cluster1/tenant1");
    }

    #[test]
    fn gen_aksk_key_new() {
        assert_eq!(gen_aksk_key("c1", "t1", true), "/yr/iam/aksk/new/c1/t1");
    }

    #[test]
    fn gen_func_meta_key_matches_cpp() {
        let key = gen_func_meta_key("default/hello/$latest");
        assert_eq!(
            key.unwrap(),
            "/yr/functions/business/yrk/tenant/default/function/hello/version/$latest"
        );
    }
}
