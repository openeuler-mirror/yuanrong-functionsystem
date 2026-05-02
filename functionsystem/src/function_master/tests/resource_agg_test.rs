use std::sync::Arc;

use yr_master::config::{AssignmentStrategy, ElectionMode, MasterConfig};
use yr_master::resource_agg::ResourceAggregator;
use yr_master::topology::TopologyManager;
use yr_proto::resources::value::{Scalar as ProtoScalar, Type as ProtoValueType};
use yr_proto::resources::{
    Resource as ProtoResource, ResourceUnit as ProtoResourceUnit, Resources as ProtoResources,
};

fn test_config() -> Arc<MasterConfig> {
    Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: "test-cluster".into(),
        election_mode: ElectionMode::Standalone,
        max_locals_per_domain: 64,
        max_domain_sched_per_domain: 1000,
        schedule_retry_sec: 10,
        domain_schedule_timeout_ms: 5000,
        enable_meta_store: false,
        meta_store_address: String::new(),
        meta_store_port: 2389,
        assignment_strategy: AssignmentStrategy::LeastLoaded,
        default_domain_address: "127.0.0.1:8401".into(),
        node_id: String::new(),
        enable_persistence: false,
        runtime_recover_enable: false,
        is_schedule_tolerate_abnormal: true,
        decrypt_algorithm: "NO_CRYPTO".into(),
        schedule_plugins: String::new(),
        migrate_enable: false,
        grace_period_seconds: 25,
        health_monitor_max_failure: 5,
        health_monitor_retry_interval: 3000,
        enable_horizontal_scale: false,
        pool_config_path: String::new(),
        domain_heartbeat_timeout: 6000,
        system_tenant_id: "0".into(),
        services_path: "/".into(),
        lib_path: "/".into(),
        function_meta_path: "/home/sn/function-metas".into(),
        enable_sync_sys_func: false,
        meta_store_mode: "local".into(),
        meta_store_max_flush_concurrency: 100,
        meta_store_max_flush_batch_size: 50,
        ssl_enable: String::new(),
        metrics_ssl_enable: String::new(),
        ssl_base_path: String::new(),
        ssl_root_file: String::new(),
        ssl_cert_file: String::new(),
        ssl_key_file: String::new(),
        aggregated_schedule_strategy: "no_aggregate".into(),
        sched_max_priority: 16,
        instance_id: "test-master".into(),
    })
}

#[tokio::test]
async fn resource_view_for_scheduler_prefers_resource_unit_allocatable() {
    let topology = TopologyManager::new(test_config(), None);
    let unit = ProtoResourceUnit {
        id: "node-a".into(),
        allocatable: Some(ProtoResources {
            resources: std::collections::HashMap::from([
                (
                    "cpu".into(),
                    ProtoResource {
                        name: "cpu".into(),
                        r#type: ProtoValueType::Scalar as i32,
                        scalar: Some(ProtoScalar {
                            value: 8.0,
                            limit: 0.0,
                        }),
                        ..Default::default()
                    },
                ),
                (
                    "memory".into(),
                    ProtoResource {
                        name: "memory".into(),
                        r#type: ProtoValueType::Scalar as i32,
                        scalar: Some(ProtoScalar {
                            value: 16.0,
                            limit: 0.0,
                        }),
                        ..Default::default()
                    },
                ),
            ]),
        }),
        ..Default::default()
    };
    topology
        .register_local(
            "node-a".into(),
            "10.0.0.1:1".into(),
            r#"{"resources":{"cpu":{"scalar":{"value":1.0}},"memory":{"scalar":{"value":2.0}}}}"#
                .into(),
            Some(unit),
            "{}".into(),
        )
        .await;

    let info = ResourceAggregator::resource_view_for_scheduler(&topology);
    assert!(info.label.contains("sum_cpu=8.0000"), "{}", info.label);
    assert!(info.label.contains("sum_mem=16.0000"), "{}", info.label);
}
