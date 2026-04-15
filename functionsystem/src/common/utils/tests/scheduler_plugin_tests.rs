//! Tests for `scheduler_framework` and `schedule_plugin`.

use std::collections::HashMap;
use yr_common::schedule_plugin::constants::{
    DEFAULT_FILTER_NAME, DEFAULT_PREFILTER_NAME, DEFAULT_SCORER_NAME,
};
use yr_common::schedule_plugin::plugins::{
    create_default_filter, create_default_prefilter, create_default_scorer,
};
use yr_common::schedule_plugin::plugin_factory::PluginFactory;
use yr_common::schedule_plugin::preallocated::PreAllocatedContext;
use yr_common::schedule_plugin::resource::{CPU_RESOURCE_NAME, MEMORY_RESOURCE_NAME};
use yr_common::schedule_plugin::plugins::register_builtin_plugins;
use yr_common::scheduler_framework::{Framework, FrameworkImpl, PolicyPlugin};
use yr_proto::resources::value::{self, Type as ValueType};
use yr_proto::resources::{InstanceInfo, Resource, ResourceUnit, Resources};

fn scalar_res(name: &str, v: f64) -> Resource {
    Resource {
        name: name.to_string(),
        r#type: ValueType::Scalar as i32,
        scalar: Some(value::Scalar { value: v, limit: 0.0 }),
        ..Default::default()
    }
}

fn make_instance(cpu: f64, mem: f64) -> InstanceInfo {
    InstanceInfo {
        instance_id: "i1".into(),
        request_id: "r1".into(),
        resources: Some(Resources {
            resources: HashMap::from([
                (CPU_RESOURCE_NAME.to_string(), scalar_res(CPU_RESOURCE_NAME, cpu)),
                (MEMORY_RESOURCE_NAME.to_string(), scalar_res(MEMORY_RESOURCE_NAME, mem)),
            ]),
        }),
        ..Default::default()
    }
}

fn make_leaf_unit(id: &str, cpu: f64, mem: f64) -> ResourceUnit {
    let cap = Resources {
        resources: HashMap::from([
            (CPU_RESOURCE_NAME.to_string(), scalar_res(CPU_RESOURCE_NAME, cpu)),
            (MEMORY_RESOURCE_NAME.to_string(), scalar_res(MEMORY_RESOURCE_NAME, mem)),
        ]),
    };
    ResourceUnit {
        id: id.to_string(),
        capacity: Some(cap.clone()),
        allocatable: Some(cap),
        fragment: HashMap::new(),
        bucket_indexs: HashMap::new(),
        status: 0,
        owner_id: id.to_string(),
        ..Default::default()
    }
}

#[test]
fn plugin_factory_register_and_create() {
    register_builtin_plugins();
    assert!(PluginFactory::create_plugin(DEFAULT_PREFILTER_NAME).is_some());
    assert!(PluginFactory::create_plugin(DEFAULT_FILTER_NAME).is_some());
    assert!(PluginFactory::create_plugin(DEFAULT_SCORER_NAME).is_some());
    assert!(PluginFactory::create_plugin("UnknownPlugin").is_none());
}

#[test]
fn default_filter_passes_when_resources_fit() {
    let mut ctx = PreAllocatedContext::default();
    let unit = make_leaf_unit("agent1", 4.0, 4096.0);
    let inst = make_instance(1.0, 1024.0);
    let filter = create_default_filter();
    let PolicyPlugin::Filter(f) = filter else {
        panic!("expected filter");
    };
    let out = f.filter(&mut ctx, &inst, &unit);
    assert!(out.status.is_ok());
    assert!(out.available_for_request > 0);
}

#[test]
fn default_scorer_higher_when_more_headroom() {
    let mut ctx = PreAllocatedContext::default();
    let inst = make_instance(1.0, 1024.0);
    let loose = make_leaf_unit("a", 8.0, 8192.0);
    let tight = make_leaf_unit("b", 2.0, 2048.0);
    let scorer = create_default_scorer();
    let PolicyPlugin::Score(s) = scorer else {
        panic!("expected scorer");
    };
    let s_loose = s.score(&mut ctx, &inst, &loose).score;
    let s_tight = s.score(&mut ctx, &inst, &tight).score;
    assert!(s_loose > s_tight, "loose={s_loose} tight={s_tight}");
}

#[test]
fn framework_select_feasible_smoke() {
    let mut fw = FrameworkImpl::default();
    let pf = create_default_prefilter();
    let df = create_default_filter();
    let ds = create_default_scorer();
    assert!(fw.register_policy(pf));
    assert!(fw.register_policy(df));
    assert!(fw.register_policy(ds));

    let mut ctx = PreAllocatedContext::default();
    let mut leaf = make_leaf_unit("leaf1", 4.0, 4096.0);
    leaf.fragment.insert("leaf1".to_string(), leaf.clone());
    let top = ResourceUnit {
        fragment: HashMap::from([("leaf1".to_string(), leaf.clone())]),
        ..Default::default()
    };
    let inst = make_instance(1.0, 1024.0);
    let res = fw.select_feasible(&mut ctx, &inst, &top, 1);
    assert_eq!(res.code, 0, "reason={}", res.reason);
    assert!(!res.sorted_feasible_nodes.is_empty());
}

#[test]
fn plugin_to_status_merge() {
    use yr_common::scheduler_framework::PluginToStatus;
    use yr_common::status::{Status, StatusCode};
    let mut m = PluginToStatus::default();
    m.add_plugin_status("a", Status::ok());
    m.add_plugin_status("b", Status::new(StatusCode::FilterPluginError, "x"));
    let merged = m.merge_status();
    assert!(merged.is_error());
}
