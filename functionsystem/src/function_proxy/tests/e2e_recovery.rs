//! E2E: proxy instance recovery — rehydrate from MetaStore + stale in-flight cleanup.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use clap::Parser;
use tokio::net::TcpListener;
use yr_common::etcd_keys::gen_instance_key;
use yr_common::types::InstanceState;
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};
use yr_metastore_server::{MetaStoreServer, MetaStoreServerConfig};
use yr_proxy::config::Config;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::instance_recover::recover_after_proxy_start;
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::state_machine::InstanceMetadata;

async fn start_metastore() -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr = listener.local_addr().unwrap();
    let mut cfg = MetaStoreServerConfig::default();
    cfg.listen_addr = addr.to_string();
    let server = MetaStoreServer::new(cfg).await.expect("server");
    let h = tokio::spawn(async move {
        let _ = server.serve(listener).await;
    });
    tokio::time::sleep(Duration::from_millis(50)).await;
    (addr, h)
}

fn meta_json(m: &InstanceMetadata) -> Vec<u8> {
    serde_json::to_vec(m).expect("serde")
}

#[tokio::test]
async fn recovery_rehydrates_running_and_marks_stale_scheduling_failed() {
    let (addr, _h) = start_metastore().await;
    let ep = format!("http://{addr}");
    let mut store = MetaStoreClient::connect(MetaStoreClientConfig::direct_etcd(ep, ""))
        .await
        .expect("connect");

    let node_id = "recover-node-e2e";
    let now = InstanceMetadata::now_ms();

    let stale = InstanceMetadata {
        id: "stale-1".into(),
        function_name: "f".into(),
        tenant: "t".into(),
        node_id: node_id.into(),
        runtime_id: String::new(),
        runtime_port: 0,
        state: InstanceState::Scheduling,
        created_at_ms: now,
        updated_at_ms: now,
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    let stale_key = gen_instance_key("t/f/1", "stale-1", "r1").expect("key");

    let running = InstanceMetadata {
        id: "run-1".into(),
        function_name: "g".into(),
        tenant: "t".into(),
        node_id: node_id.into(),
        runtime_id: "rt-1".into(),
        runtime_port: 9001,
        state: InstanceState::Running,
        created_at_ms: now,
        updated_at_ms: now,
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::from([("cpu".into(), 2.0), ("memory".into(), 512.0)]),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    let run_key = gen_instance_key("t/g/1", "run-1", "r2").expect("key");

    store.put(&stale_key, &meta_json(&stale)).await.unwrap();
    store.put(&run_key, &meta_json(&running)).await.unwrap();

    let cfg = Arc::new(
        Config::try_parse_from(["yr-proxy", "--node-id", node_id, "--grpc-listen-port", "1"])
            .unwrap(),
    );
    let rv = ResourceView::new(ResourceVector {
        cpu: 16.0,
        memory: 1024.0,
        npu: 0.0,
    });
    let ctrl = InstanceController::new(cfg, rv, None, None);

    let summary = recover_after_proxy_start(&ctrl, &mut store).await;
    assert_eq!(summary.rehydrated, 2);
    assert_eq!(summary.stale_in_flight_marked_failed, 1);

    let s = ctrl.get("stale-1").expect("stale row");
    assert_eq!(s.state, InstanceState::Failed);

    let r = ctrl.get("run-1").expect("running row");
    assert_eq!(r.state, InstanceState::Running);
    assert_eq!(r.runtime_id, "rt-1");
}
