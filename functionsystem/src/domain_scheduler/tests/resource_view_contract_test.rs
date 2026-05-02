use base64::Engine;
use prost::Message;
use serde_json::json;
use yr_domain_scheduler::resource_view::{merge_topology_resource, ResourceView};
use yr_proto::resources::value::{Counter, Scalar};
use yr_proto::resources::{Resource, ResourceUnit as ProtoResourceUnit, Resources};

fn find_node<'a>(summary: &'a serde_json::Value, node_id: &str) -> &'a serde_json::Value {
    summary["nodes"]
        .as_array()
        .and_then(|nodes| nodes.iter().find(|node| node["node_id"] == node_id))
        .expect("node summary present")
}

#[test]
fn resource_view_summary_preserves_cpp_style_vectors_labels_and_instance_actual_use() {
    let view = ResourceView::new();
    let resource_json = json!({
        "capacity": {
            "cpu": 8000.0,
            "memory": 32768.0,
            "GPU/Ascend910B4": 2.0
        },
        "used": {
            "cpu": 1000.0,
            "memory": 2048.0,
            "GPU/Ascend910B4": 1.0
        },
        "allocatable": {
            "cpu": 7000.0,
            "memory": 30720.0,
            "GPU/Ascend910B4": 2.0
        },
        "labels": {
            "zone": "az1",
            "NODE_ID": "node-a"
        },
        "vectors": {
            "GPU/Ascend910B4": {
                "values": {
                    "ids": {
                        "vectors": {
                            "node-a": { "values": [0.0, 1.0] }
                        }
                    },
                    "HBM": {
                        "vectors": {
                            "node-a": { "values": [1000.0, 1000.0] }
                        }
                    }
                },
                "heterogeneousInfo": {
                    "vendor": "huawei.com",
                    "product_model": "Ascend910B4"
                }
            }
        },
        "instances": {
            "inst-1": {
                "instanceid": "inst-1",
                "actualUse": {
                    "resources": {
                        "GPU/Ascend910B4": {
                            "name": "GPU/Ascend910B4"
                        }
                    }
                }
            }
        }
    });

    merge_topology_resource("node-a", &resource_json.to_string(), &view);

    let summary = view.domain_summary();
    let node = find_node(&summary, "node-a");
    assert_eq!(node["capacity"]["cpu"].as_f64(), Some(8000.0));
    assert_eq!(node["allocatable"]["memory"].as_f64(), Some(30720.0));
    assert_eq!(node["labels"]["zone"].as_str(), Some("az1"));
    assert_eq!(
        node["vectors"]["GPU/Ascend910B4"]["heterogeneousInfo"]["vendor"].as_str(),
        Some("huawei.com")
    );
    assert_eq!(
        node["instances"]["inst-1"]["actualUse"]["resources"]["GPU/Ascend910B4"]["name"].as_str(),
        Some("GPU/Ascend910B4")
    );
}

#[test]
fn resource_view_proto_labels_choose_stable_value_when_counter_has_multiple_items() {
    let view = ResourceView::new();
    let unit = ProtoResourceUnit {
        id: "node-b".into(),
        capacity: Some(Resources {
            resources: std::collections::HashMap::from([(
                "cpu".into(),
                Resource {
                    name: "cpu".into(),
                    scalar: Some(Scalar {
                        value: 4.0,
                        limit: 0.0,
                    }),
                    ..Default::default()
                },
            )]),
        }),
        node_labels: std::collections::HashMap::from([(
            "zone".into(),
            Counter {
                items: std::collections::HashMap::from([
                    ("az2".into(), 1_u64),
                    ("az1".into(), 1_u64),
                ]),
            },
        )]),
        ..Default::default()
    };
    let encoded = base64::engine::general_purpose::STANDARD.encode(unit.encode_to_vec());
    yr_domain_scheduler::resource_view::merge_topology_resource_unit_b64("node-b", &encoded, &view);

    let summary = view.domain_summary();
    let node = find_node(&summary, "node-b");
    assert_eq!(node["labels"]["zone"].as_str(), Some("az1"));
}

#[test]
fn resource_view_proto_instances_preserve_actual_use_scalar_payloads() {
    let view = ResourceView::new();
    let unit = ProtoResourceUnit {
        id: "node-c".into(),
        instances: std::collections::HashMap::from([(
            "inst-2".into(),
            yr_proto::resources::InstanceInfo {
                instance_id: "inst-2".into(),
                actual_use: Some(Resources {
                    resources: std::collections::HashMap::from([(
                        "cpu".into(),
                        Resource {
                            name: "cpu".into(),
                            scalar: Some(Scalar {
                                value: 1500.0,
                                limit: 0.0,
                            }),
                            ..Default::default()
                        },
                    )]),
                }),
                ..Default::default()
            },
        )]),
        ..Default::default()
    };
    let encoded = base64::engine::general_purpose::STANDARD.encode(unit.encode_to_vec());
    yr_domain_scheduler::resource_view::merge_topology_resource_unit_b64("node-c", &encoded, &view);

    let summary = view.domain_summary();
    let node = find_node(&summary, "node-c");
    assert_eq!(
        node["instances"]["inst-2"]["actualUse"]["resources"]["cpu"]["scalar"]["value"].as_f64(),
        Some(1500.0)
    );
}
