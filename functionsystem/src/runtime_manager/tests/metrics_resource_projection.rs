use std::collections::BTreeMap;
use std::path::PathBuf;

use yr_runtime_manager::config::Config;
use yr_runtime_manager::metrics::{
    build_numa_vectors, build_resource_projection, collect_node_labels_from_sources,
    collect_numa_cpu_counts_from_root, InstanceMetric, NodeMetricsSample,
};

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
    assert!(p.labels.is_empty());
    assert!(p.vectors.is_empty());
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

#[test]
fn node_labels_follow_cpp_resource_label_sources() {
    let dir = std::env::temp_dir().join(format!(
        "yr_rm_labels_{}_{}",
        std::process::id(),
        "resource_projection"
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let labels_file = dir.join("labels");
    std::fs::write(
        &labels_file,
        "zone=\"cn-south-1\"\ninvalid\nteam=\"runtime\"\nempty=\n",
    )
    .unwrap();

    let labels = collect_node_labels_from_sources(
        Some(r#"{"init":"true","rank":"3","ignored_number":3}"#),
        Some("node-7"),
        Some("10.0.0.7"),
        &labels_file,
    );

    assert_eq!(labels.get("init").map(String::as_str), Some("true"));
    assert_eq!(labels.get("rank").map(String::as_str), Some("3"));
    assert!(!labels.contains_key("ignored_number"));
    assert_eq!(labels.get("NODE_ID").map(String::as_str), Some("node-7"));
    assert_eq!(labels.get("HOST_IP").map(String::as_str), Some("10.0.0.7"));
    assert_eq!(labels.get("zone").map(String::as_str), Some("cn-south-1"));
    assert_eq!(labels.get("team").map(String::as_str), Some("runtime"));
    assert!(!labels.contains_key("empty"));

    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn numa_vectors_follow_cpp_resource_shape() {
    let (count, vector) = build_numa_vectors("node-a", &[(0, 2), (1, 4)]).expect("numa vectors");

    assert_eq!(count, 2.0);
    assert_eq!(
        vector.values["ids"].vectors["node-a"].values,
        vec![0.0, 1.0]
    );
    assert_eq!(
        vector.values["CPU"].vectors["node-a"].values,
        vec![2000.0, 4000.0]
    );
}

#[test]
fn numa_cpu_counts_parse_sysfs_cpulists() {
    let dir = std::env::temp_dir().join(format!(
        "yr_rm_numa_{}_{}",
        std::process::id(),
        "resource_projection"
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(dir.join("node1")).unwrap();
    std::fs::create_dir_all(dir.join("node0")).unwrap();
    std::fs::write(dir.join("node1/cpulist"), "4-7,10\n").unwrap();
    std::fs::write(dir.join("node0/cpulist"), "0,2-3\n").unwrap();

    let counts = collect_numa_cpu_counts_from_root(&dir);

    assert_eq!(counts, vec![(0, 3), (1, 5)]);

    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn numa_projection_is_flag_gated() {
    let mut cfg = test_config();
    let node = NodeMetricsSample::default();
    assert!(!build_resource_projection(&cfg, &node, &[])
        .capacity
        .contains_key("NUMA"));

    cfg.numa_collection_enable = true;
    let projection = build_resource_projection(&cfg, &node, &[]);

    // Host may expose zero or more NUMA nodes; this assertion locks the gate:
    // if projected, scalar capacity and vector resource appear together.
    assert_eq!(
        projection.capacity.contains_key("NUMA"),
        projection.vectors.contains_key("NUMA")
    );
}
