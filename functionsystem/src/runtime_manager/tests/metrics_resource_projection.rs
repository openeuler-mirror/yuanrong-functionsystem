use std::collections::BTreeMap;
use std::path::PathBuf;

use yr_runtime_manager::config::Config;
use yr_runtime_manager::metrics::{
    build_disk_vectors, build_gpu_vectors_from_ids, build_gpu_vectors_from_ids_visible,
    build_npu_count_vectors_from_ids, build_npu_count_vectors_from_ids_visible,
    build_npu_topology_vectors_from_json, build_npu_topology_vectors_from_json_visible,
    build_numa_vectors, build_resource_projection, collect_node_labels_from_sources,
    collect_numa_cpu_counts_from_root, parse_gpu_query_csv, InstanceMetric, NodeMetricsSample,
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

#[test]
fn disk_vectors_follow_cpp_disk_resources_shape() {
    let config = r#"[
        {"name":"fast","size":"40G","mountPoints":"/mnt/fast/"},
        {"name":"bulk","size":"100G","mountPoints":"/mnt/bulk/"}
    ]"#;

    let vector = build_disk_vectors("node-a", config).expect("disk vector");

    assert_eq!(
        vector.values["disk"].vectors["node-a"].values,
        vec![40.0, 100.0]
    );
    assert_eq!(vector.extensions.len(), 2);
    assert_eq!(vector.extensions[0].disk.name, "fast");
    assert_eq!(vector.extensions[0].disk.size, 40);
    assert_eq!(vector.extensions[0].disk.mount_points, "/mnt/fast/");
    assert_eq!(vector.extensions[1].disk.name, "bulk");
    assert_eq!(vector.extensions[1].disk.size, 100);
    assert_eq!(vector.extensions[1].disk.mount_points, "/mnt/bulk/");
}

#[test]
fn disk_vectors_skip_invalid_entries_like_cpp() {
    let config = r#"[
        {"name":"ok","size":"8G","mountPoints":"/mnt/ok/"},
        {"name":"bad_size","size":"8M","mountPoints":"/mnt/bad/"},
        {"name":"bad_path","size":"9G","mountPoints":"/mnt/../bad/"},
        {"name":"bad_root","size":"10G","mountPoints":"/"},
        {"name":"bad_double_slash","size":"11G","mountPoints":"//"},
        {"name":"missing_path","size":"12G"}
    ]"#;

    let vector = build_disk_vectors("node-a", config).expect("one valid disk vector");

    assert_eq!(vector.values["disk"].vectors["node-a"].values, vec![8.0]);
    assert_eq!(vector.extensions.len(), 1);
    assert_eq!(vector.extensions[0].disk.name, "ok");
}

#[test]
fn disk_projection_is_config_gated_and_preserves_scalar_root_disk() {
    let mut cfg = test_config();
    let node = NodeMetricsSample {
        disk_total_bytes: 20 * 1024 * 1024 * 1024,
        disk_avail_bytes: 15 * 1024 * 1024 * 1024,
        ..Default::default()
    };
    assert!(!build_resource_projection(&cfg, &node, &[])
        .vectors
        .contains_key("disk"));

    cfg.disk_resources = r#"[{"name":"fast","size":"40G","mountPoints":"/mnt/fast/"}]"#.into();
    let projection = build_resource_projection(&cfg, &node, &[]);

    assert_eq!(projection.capacity.get("disk"), Some(&20.0));
    assert_eq!(projection.used.get("disk"), Some(&5.0));
    assert_eq!(
        projection.vectors["disk"].values["disk"].vectors["node-a"].values,
        vec![40.0]
    );
    assert_eq!(projection.vectors["disk"].extensions[0].disk.name, "fast");
}

#[test]
fn xpu_vectors_follow_cpp_heterogeneous_resource_shape() {
    let (name, count, vector) =
        build_npu_count_vectors_from_ids("node-a", &[0, 2]).expect("npu count vectors");

    assert_eq!(name, "NPU/Ascend");
    assert_eq!(count, 2.0);
    assert_eq!(
        vector.values["ids"].vectors["node-a"].values,
        vec![0.0, 2.0]
    );
    assert_eq!(
        vector.values["HBM"].vectors["node-a"].values,
        vec![1000.0, 1000.0]
    );
    assert_eq!(
        vector.values["stream"].vectors["node-a"].values,
        vec![110.0, 110.0]
    );
    assert_eq!(
        vector.values["latency"].vectors["node-a"].values,
        vec![0.0, 0.0]
    );
    assert_eq!(
        vector.values["health"].vectors["node-a"].values,
        vec![0.0, 0.0]
    );
    assert_eq!(
        vector.heterogeneous_info.get("vendor").map(String::as_str),
        Some("huawei.com")
    );
    assert_eq!(
        vector
            .heterogeneous_info
            .get("product_model")
            .map(String::as_str),
        Some("Ascend")
    );
}

#[test]
fn gpu_vectors_are_available_for_flag_gated_projection_inputs() {
    let (name, count, vector) = build_gpu_vectors_from_ids("node-a", &[0, 1]).expect("gpu vectors");

    assert_eq!(name, "GPU/cuda");
    assert_eq!(count, 2.0);
    assert_eq!(
        vector.values["ids"].vectors["node-a"].values,
        vec![0.0, 1.0]
    );
    assert_eq!(
        vector.heterogeneous_info.get("vendor").map(String::as_str),
        Some("nvidia.com")
    );
    assert_eq!(
        vector
            .heterogeneous_info
            .get("product_model")
            .map(String::as_str),
        Some("cuda")
    );
}

#[test]
fn gpu_query_csv_parser_skips_malformed_rows_and_keeps_first_model() {
    let csv = "\
0, NVIDIA A800, 81920, 4096\n\
bad,row\n\
2, NVIDIA A800, 81920, 0\n";

    let (name, count, vector) = parse_gpu_query_csv("node-a", csv, None).expect("gpu vectors");

    assert_eq!(name, "GPU/NVIDIA A800");
    assert_eq!(count, 2.0);
    assert_eq!(
        vector.values["ids"].vectors["node-a"].values,
        vec![0.0, 2.0]
    );
    assert_eq!(
        vector.values["HBM"].vectors["node-a"].values,
        vec![81920.0, 81920.0]
    );
    assert_eq!(
        vector.values["usedHBM"].vectors["node-a"].values,
        vec![4096.0, 0.0]
    );
}

#[test]
fn gpu_visible_devices_filter_matches_cpp_slot_semantics() {
    let (name, count, vector) =
        build_gpu_vectors_from_ids_visible("node-a", &[3, 5], Some("1")).expect("gpu visible filter");

    assert_eq!(name, "GPU/cuda");
    assert_eq!(count, 1.0);
    assert_eq!(vector.values["ids"].vectors["node-a"].values, vec![5.0]);
}

#[test]
fn gpu_visible_devices_invalid_env_falls_back_to_detected_result() {
    let (_, count, vector) =
        build_gpu_vectors_from_ids_visible("node-a", &[3, 5], Some("oops")).expect("gpu fallback");

    assert_eq!(count, 2.0);
    assert_eq!(vector.values["ids"].vectors["node-a"].values, vec![3.0, 5.0]);
}

#[test]
fn npu_topology_json_fallback_matches_cpp_node_filter_and_partition_shape() {
    let json = r#"{
        "worker-a": {
            "nodeName": "node-a",
            "number": 2,
            "vDeviceIDs": [3, 5],
            "vDevicePartition": ["0", "1"]
        },
        "worker-b": {
            "nodeName": "node-b",
            "number": 1,
            "vDeviceIDs": [9],
            "vDevicePartition": ["0"]
        }
    }"#;

    let (name, count, vector) =
        build_npu_topology_vectors_from_json("node-a", json).expect("topology fallback");

    assert_eq!(name, "NPU");
    assert_eq!(count, 2.0);
    assert_eq!(
        vector.values["ids"].vectors["node-a"].values,
        vec![3.0, 5.0]
    );
    assert_eq!(
        vector
            .heterogeneous_info
            .get("partition")
            .map(String::as_str),
        Some("0,1")
    );
    assert!(build_npu_topology_vectors_from_json("missing-node", json).is_none());
}

#[test]
fn npu_count_visible_devices_filter_matches_cpp_slot_semantics() {
    let (name, count, vector) = build_npu_count_vectors_from_ids_visible("node-a", &[10, 12], Some("1"))
        .expect("npu count visible filter");

    assert_eq!(name, "NPU/Ascend");
    assert_eq!(count, 1.0);
    assert_eq!(vector.values["ids"].vectors["node-a"].values, vec![12.0]);
    assert_eq!(vector.values["HBM"].vectors["node-a"].values, vec![1000.0]);
}

#[test]
fn npu_topology_visible_devices_filter_matches_cpp_slot_semantics() {
    let json = r#"{
        "worker-a": {
            "nodeName": "node-a",
            "number": 2,
            "vDeviceIDs": [3, 5],
            "vDevicePartition": ["left", "right"]
        }
    }"#;

    let (name, count, vector) =
        build_npu_topology_vectors_from_json_visible("node-a", json, Some("1"))
            .expect("topology visible filter");

    assert_eq!(name, "NPU");
    assert_eq!(count, 1.0);
    assert_eq!(vector.values["ids"].vectors["node-a"].values, vec![5.0]);
    assert_eq!(
        vector.heterogeneous_info.get("partition").map(String::as_str),
        Some("right")
    );
}

#[test]
fn npu_topology_fallback_is_wired_into_resource_projection_for_supported_modes() {
    let dir = std::env::temp_dir().join(format!(
        "yr_rm_npu_topology_{}_{}",
        std::process::id(),
        "resource_projection"
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("topology-info.json");
    std::fs::write(
        &path,
        r#"{
            "worker-a": {
                "nodeName": "node-a",
                "number": 2,
                "vDeviceIDs": [1, 4],
                "vDevicePartition": ["left", "right"]
            }
        }"#,
    )
    .unwrap();

    let mut cfg = test_config();
    cfg.npu_device_info_path = path;
    cfg.npu_collection_mode = "all".into();
    let projection = build_resource_projection(&cfg, &NodeMetricsSample::default(), &[]);

    assert_eq!(projection.capacity.get("NPU"), Some(&2.0));
    assert_eq!(
        projection.vectors["NPU"].values["ids"].vectors["node-a"].values,
        vec![1.0, 4.0]
    );
    assert_eq!(
        projection.vectors["NPU"]
            .heterogeneous_info
            .get("partition")
            .map(String::as_str),
        Some("left,right")
    );

    cfg.npu_collection_mode = "unsupported".into();
    let projection = build_resource_projection(&cfg, &NodeMetricsSample::default(), &[]);
    assert!(!projection.capacity.contains_key("NPU"));
    assert!(!projection.vectors.contains_key("NPU"));

    let _ = std::fs::remove_dir_all(&dir);
}
