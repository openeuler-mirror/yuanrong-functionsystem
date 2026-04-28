use std::collections::BTreeMap;
use std::path::PathBuf;

use yr_runtime_manager::config::Config;
use yr_runtime_manager::metrics::{build_resource_projection, InstanceMetric, NodeMetricsSample};

fn test_config() -> Config {
    Config::embedded_in_agent(
        "node-a".into(),
        "http://127.0.0.1:8403".into(),
        "/bin/sleep".into(),
        9000,
        100,
        PathBuf::from("/tmp/yr-test-logs"),
        String::new(),
    )
}

#[test]
fn proc_metrics_projection_uses_cpp_default_capacity_and_instance_rss_mb() {
    let mut cfg = test_config();
    cfg.custom_resources = r#"{"npu":2}"#.into();
    let node = NodeMetricsSample {
        disk_total_bytes: 20 * 1024 * 1024 * 1024,
        disk_avail_bytes: 15 * 1024 * 1024 * 1024,
        ..Default::default()
    };
    let instances = vec![InstanceMetric {
        instance_id: "i1".into(),
        runtime_id: "r1".into(),
        pid: 123,
        rss_kb: 512 * 1024,
        port: 9000,
        net_rx_bytes: 0,
        net_tx_bytes: 0,
        resource_limits: BTreeMap::from([("memory".into(), 500.0)]),
    }];

    let p = build_resource_projection(&cfg, &node, &instances);

    assert_eq!(p.capacity.get("cpu"), Some(&1000.0));
    assert_eq!(p.capacity.get("memory"), Some(&4000.0));
    assert_eq!(p.capacity.get("npu"), Some(&2.0));
    assert_eq!(p.used.get("memory"), Some(&512.0));
    assert_eq!(p.capacity.get("disk"), Some(&20.0));
    assert_eq!(p.used.get("disk"), Some(&5.0));
    assert_eq!(p.resources["cpu"].scalar.value, 1000.0);
    assert_eq!(p.resources["memory"].scalar.value, 4000.0);
}

#[test]
fn node_metrics_projection_uses_host_memory_minus_cpp_overhead() {
    let mut cfg = test_config();
    cfg.metrics_collector_type = "node".into();
    cfg.overhead_memory = 128.0;
    let node = NodeMetricsSample {
        memory_total_kb: 1024 * 1024,
        memory_available_kb: 768 * 1024,
        ..Default::default()
    };

    let p = build_resource_projection(&cfg, &node, &[]);

    assert_eq!(p.capacity.get("memory"), Some(&896.0));
    assert_eq!(p.used.get("memory"), Some(&256.0));
    assert!(p.capacity.get("cpu").copied().unwrap_or_default() > 0.0);
}
