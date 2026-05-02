use std::collections::BTreeMap;

use yr_proto::resources::value::Type as ProtoValueType;
use yr_runtime_manager::metrics::{
    build_resource_unit, build_resource_update_json, DiskContent, InstanceMetric, MetricsSnapshot,
    NodeMetricsSample, ResourceExtension, ResourceProjection, VectorCategory, VectorResource,
    VectorValues,
};

#[test]
fn resource_update_payload_and_proto_unit_preserve_vectors_labels_and_instance_usage() {
    let snap = MetricsSnapshot {
        node_id: "node-a".into(),
        node: NodeMetricsSample::default(),
        instances: vec![InstanceMetric {
            instance_id: "inst-1".into(),
            runtime_id: "rt-1".into(),
            pid: 42,
            rss_kb: 2048,
            port: 30123,
            net_rx_bytes: 10,
            net_tx_bytes: 20,
            resource_limits: BTreeMap::from([("cpu".into(), 2000.0), ("memory".into(), 1024.0)]),
        }],
        accelerators: Default::default(),
        resource_projection: ResourceProjection {
            capacity: BTreeMap::from([
                ("cpu".into(), 8000.0),
                ("memory".into(), 32768.0),
                ("GPU/Ascend910B4".into(), 2.0),
            ]),
            used: BTreeMap::from([
                ("cpu".into(), 1000.0),
                ("memory".into(), 2048.0),
                ("GPU/Ascend910B4".into(), 1.0),
            ]),
            allocatable: BTreeMap::from([
                ("cpu".into(), 7000.0),
                ("memory".into(), 30720.0),
                ("GPU/Ascend910B4".into(), 2.0),
            ]),
            labels: BTreeMap::from([("zone".into(), "az1".into())]),
            vectors: BTreeMap::from([(
                "GPU/Ascend910B4".into(),
                VectorResource {
                    values: BTreeMap::from([
                        (
                            "ids".into(),
                            VectorCategory {
                                vectors: BTreeMap::from([(
                                    "node-a".into(),
                                    VectorValues {
                                        values: vec![0.0, 1.0],
                                    },
                                )]),
                            },
                        ),
                        (
                            "HBM".into(),
                            VectorCategory {
                                vectors: BTreeMap::from([(
                                    "node-a".into(),
                                    VectorValues {
                                        values: vec![1000.0, 1000.0],
                                    },
                                )]),
                            },
                        ),
                    ]),
                    heterogeneous_info: BTreeMap::from([
                        ("vendor".into(), "huawei.com".into()),
                        ("product_model".into(), "Ascend910B4".into()),
                    ]),
                    extensions: vec![ResourceExtension {
                        disk: DiskContent {
                            name: "nvme0n1".into(),
                            size: 80,
                            mount_points: "/data/".into(),
                        },
                    }],
                },
            )]),
            resources: BTreeMap::new(),
        },
    };

    let json = build_resource_update_json(&snap);
    assert_eq!(json["allocatable"]["memory"].as_f64(), Some(30720.0));
    assert_eq!(json["labels"]["zone"].as_str(), Some("az1"));
    assert_eq!(
        json["vectors"]["GPU/Ascend910B4"]["heterogeneousInfo"]["vendor"].as_str(),
        Some("huawei.com")
    );
    assert_eq!(
        json["instances"]["inst-1"]["actualUse"]["resources"]["cpu"]["scalar"]["value"].as_f64(),
        Some(2000.0)
    );

    let unit = build_resource_unit(&snap);
    assert_eq!(unit.id, "node-a");
    let allocatable = unit.allocatable.as_ref().expect("allocatable");
    assert_eq!(
        allocatable
            .resources
            .get("cpu")
            .and_then(|resource| resource.scalar.as_ref())
            .map(|scalar| scalar.value),
        Some(7000.0)
    );
    let gpu = allocatable
        .resources
        .get("GPU/Ascend910B4")
        .expect("gpu resource");
    assert_eq!(gpu.r#type, ProtoValueType::Vectors as i32);
    assert_eq!(
        gpu.heterogeneous_info.get("vendor").map(String::as_str),
        Some("huawei.com")
    );
    assert_eq!(
        unit.node_labels
            .get("zone")
            .and_then(|counter| counter.items.get("az1"))
            .copied(),
        Some(1)
    );
    assert_eq!(
        unit.instances
            .get("inst-1")
            .and_then(|instance| instance.actual_use.as_ref())
            .and_then(|resources| resources.resources.get("cpu"))
            .and_then(|resource| resource.scalar.as_ref())
            .map(|scalar| scalar.value),
        Some(2000.0)
    );
    let gpu_actual_use = unit
        .actual_use
        .as_ref()
        .and_then(|resources| resources.resources.get("GPU/Ascend910B4"))
        .expect("gpu actual use resource");
    assert_eq!(gpu_actual_use.r#type, ProtoValueType::Scalar as i32);
    assert_eq!(
        gpu_actual_use.scalar.as_ref().map(|scalar| scalar.value),
        Some(1.0)
    );
}

#[test]
fn xpu_capacity_is_not_reported_as_fully_used_by_default() {
    let snap = MetricsSnapshot {
        node_id: "node-xpu".into(),
        node: NodeMetricsSample::default(),
        instances: Vec::new(),
        accelerators: Default::default(),
        resource_projection: ResourceProjection {
            capacity: BTreeMap::from([("GPU/Test".into(), 2.0)]),
            used: BTreeMap::from([("GPU/Test".into(), 0.0)]),
            allocatable: BTreeMap::from([("GPU/Test".into(), 2.0)]),
            labels: BTreeMap::new(),
            vectors: BTreeMap::from([(
                "GPU/Test".into(),
                VectorResource {
                    values: BTreeMap::new(),
                    heterogeneous_info: BTreeMap::new(),
                    extensions: Vec::new(),
                },
            )]),
            resources: BTreeMap::new(),
        },
    };

    let unit = build_resource_unit(&snap);
    let gpu = unit
        .actual_use
        .as_ref()
        .and_then(|resources| resources.resources.get("GPU/Test"))
        .expect("gpu actual use");
    assert_eq!(gpu.r#type, ProtoValueType::Scalar as i32);
    assert_eq!(gpu.scalar.as_ref().map(|scalar| scalar.value), Some(0.0));
}
