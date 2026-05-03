//! E2E: embedded MetaStore server with etcd-compatible gRPC (KV, range, watch, lease, txn).

use std::time::Duration;

use etcd_client::{
    Compare, CompareOp, EventType, GetOptions, PutOptions, Txn as EtcdTxn, TxnOp, WatchOptions,
};
use tokio::net::TcpListener;
use tonic::transport::Channel;
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};
use yr_metastore_server::{MetaStoreServer, MetaStoreServerConfig};
use yr_proto::metastore::{KeepAliveOnceRequest, meta_store_service_client::MetaStoreServiceClient};

async fn start_server() -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
    start_server_with_config(MetaStoreServerConfig::default()).await
}

async fn start_server_with_config(
    mut cfg: MetaStoreServerConfig,
) -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr = listener.local_addr().expect("addr");
    cfg.listen_addr = addr.to_string();
    let server = MetaStoreServer::new(cfg).await.expect("MetaStoreServer::new");
    let h = tokio::spawn(async move {
        let _ = server.serve(listener).await;
    });
    tokio::time::sleep(Duration::from_millis(50)).await;
    (addr, h)
}

async fn start_server_at_addr_with_config(
    addr: std::net::SocketAddr,
    mut cfg: MetaStoreServerConfig,
) -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let listener = loop {
        match TcpListener::bind(addr).await {
            Ok(listener) => break listener,
            Err(err) if tokio::time::Instant::now() < deadline => {
                let _ = err;
                tokio::time::sleep(Duration::from_millis(50)).await;
            }
            Err(err) => panic!("bind {addr}: {err}"),
        }
    };
    cfg.listen_addr = addr.to_string();
    let server = MetaStoreServer::new(cfg).await.expect("MetaStoreServer::new");
    let h = tokio::spawn(async move {
        let _ = server.serve(listener).await;
    });
    tokio::time::sleep(Duration::from_millis(50)).await;
    (addr, h)
}

fn etcd_endpoint(addr: std::net::SocketAddr) -> String {
    format!("http://{addr}")
}

async fn grpc_client(addr: std::net::SocketAddr) -> MetaStoreServiceClient<Channel> {
    MetaStoreServiceClient::connect(etcd_endpoint(addr))
        .await
        .expect("grpc connect")
}

#[tokio::test]
async fn kv_put_get_delete_round_trip() {
    let (addr, _h) = start_server().await;
    let etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
        .await
        .expect("etcd connect");
    let key = b"e2e/kv/a";
    let mut kv = etcd.kv_client();
    kv.put(key, b"v1", None).await.expect("put");
    let g = kv.get(key, None).await.expect("get");
    assert_eq!(g.kvs().first().map(|k| k.value()), Some(b"v1".as_slice()));
    kv.delete(key, None).await.expect("delete");
    let g2 = kv.get(key, None).await.expect("get after del");
    assert!(g2.kvs().is_empty());
}

#[tokio::test]
async fn range_prefix_lists_keys() {
    let (addr, _h) = start_server().await;
    let etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
        .await
        .expect("connect");
    let mut kv = etcd.kv_client();
    kv.put(b"e2e/range/x/1", b"a", None).await.unwrap();
    kv.put(b"e2e/range/x/2", b"b", None).await.unwrap();
    kv.put(b"e2e/other", b"c", None).await.unwrap();
    let r = kv
        .get(
            b"e2e/range/",
            Some(GetOptions::new().with_prefix().with_sort(
                etcd_client::SortTarget::Key,
                etcd_client::SortOrder::Ascend,
            )),
        )
        .await
        .expect("range");
    let keys: Vec<_> = r.kvs().iter().map(|k| k.key().to_vec()).collect();
    assert_eq!(keys.len(), 2);
    assert!(keys[0].ends_with(b"/1"));
    assert!(keys[1].ends_with(b"/2"));
}

#[tokio::test]
async fn watch_delivers_put_event() {
    let (addr, _h) = start_server().await;
    let ep = etcd_endpoint(addr);
    let etcd = etcd_client::Client::connect([ep.clone()], None)
        .await
        .expect("connect");
    let key = b"e2e/watch/k";
    let (_w, mut stream) = etcd
        .watch_client()
        .watch(key, Some(WatchOptions::new()))
        .await
        .expect("watch");

    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(80)).await;
        let c = etcd_client::Client::connect([ep], None).await.unwrap();
        c.kv_client()
            .put(key, b"hello", None)
            .await
            .unwrap();
    });

    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let mut saw_put = false;
    while tokio::time::Instant::now() < deadline {
        match tokio::time::timeout(Duration::from_millis(500), stream.message()).await {
            Err(_) => continue,
            Ok(Err(_)) => break,
            Ok(Ok(None)) => break,
            Ok(Ok(Some(msg))) => {
                for ev in msg.events() {
                    if ev.event_type() == EventType::Put {
                        if let Some(kv) = ev.kv() {
                            if kv.value() == b"hello" {
                                saw_put = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        if saw_put {
            break;
        }
    }
    assert!(saw_put, "expected PUT watch for hello");
}

#[tokio::test]
async fn lease_grant_put_revoke() {
    let (addr, _h) = start_server().await;
    let etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
        .await
        .expect("connect");
    let lease_resp = etcd.lease_client().grant(120, None).await.expect("grant");
    let lease = lease_resp.id();
    let key = b"e2e/lease/k";
    let mut kv = etcd.kv_client();
    kv.put(
        key,
        b"bound",
        Some(PutOptions::new().with_lease(lease)),
    )
    .await
    .expect("put with lease");
    etcd.lease_client()
        .revoke(lease)
        .await
        .expect("revoke");
    let g = kv.get(key, None).await.expect("get");
    assert!(
        g.kvs().is_empty(),
        "key should disappear after lease revoke"
    );
}

#[tokio::test]
async fn txn_compare_and_swap_on_mod_revision() {
    let (addr, _h) = start_server().await;
    let etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
        .await
        .expect("connect");
    let key = b"e2e/txn/cas";
    let mut kv = etcd.kv_client();
    kv.put(key, b"v0", None).await.unwrap();
    let rev0 = kv
        .get(key, None)
        .await
        .unwrap()
        .kvs()
        .first()
        .map(|k| k.mod_revision())
        .expect("mod_revision");

    let fail = etcd
        .kv_client()
        .txn(
            EtcdTxn::new()
                .when([Compare::mod_revision(
                    key.to_vec(),
                    CompareOp::Equal,
                    rev0 + 999,
                )])
                .and_then([TxnOp::put(key, b"bad", None)])
                .or_else([]),
        )
        .await
        .unwrap();
    assert!(!fail.succeeded());

    let ok = etcd
        .kv_client()
        .txn(
            EtcdTxn::new()
                .when([Compare::mod_revision(
                    key.to_vec(),
                    CompareOp::Equal,
                    rev0,
                )])
                .and_then([TxnOp::put(key, b"v1", None)])
                .or_else([]),
        )
        .await
        .unwrap();
    assert!(ok.succeeded());
    let g = kv.get(key, None).await.unwrap();
    assert_eq!(g.kvs().first().map(|k| k.value()), Some(b"v1".as_slice()));
}

#[tokio::test]
async fn metastore_client_routes_through_same_server() {
    let (addr, _h) = start_server().await;
    let mut c = MetaStoreClient::connect(MetaStoreClientConfig::direct_etcd(
        etcd_endpoint(addr),
        "",
    ))
    .await
    .expect("MetaStoreClient");
    c.put("logical/x", b"42").await.expect("put");
    let g = c.get("logical/x").await.expect("get");
    assert_eq!(g.kvs.len(), 1);
    assert_eq!(g.kvs[0].value, b"42");
}

#[tokio::test]
async fn lease_recovered_from_backup_after_restart() {
    let (backup_addr, _backup_h) = start_server().await;

    let mut cfg = MetaStoreServerConfig::default();
    cfg.etcd_endpoints = vec![etcd_endpoint(backup_addr)];
    let (primary_addr, primary_h) = start_server_with_config(cfg.clone()).await;

    let mut primary = grpc_client(primary_addr).await;
    let lease_id = primary
        .grant_lease(yr_proto::metastore::GrantLeaseRequest { ttl: 120 })
        .await
        .expect("grant lease")
        .into_inner()
        .id;
    assert!(lease_id > 0);

    let backup = etcd_client::Client::connect([etcd_endpoint(backup_addr)], None)
        .await
        .expect("connect backup etcd");
    let backup_key = format!("{}/{}", cfg.lease_backup_prefix.trim_end_matches('/'), lease_id);
    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let mut persisted = false;
    while tokio::time::Instant::now() < deadline {
        let resp = backup
            .kv_client()
            .get(backup_key.as_bytes(), None)
            .await
            .expect("get backup key");
        if !resp.kvs().is_empty() {
            persisted = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(persisted, "lease backup entry was not persisted");

    primary_h.abort();
    tokio::time::sleep(Duration::from_millis(100)).await;

    let (restarted_addr, _restarted_h) = start_server_with_config(cfg).await;
    let mut restarted = grpc_client(restarted_addr).await;
    let keepalive = restarted
        .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
        .await
        .expect("keep alive once")
        .into_inner();

    assert_eq!(keepalive.id, lease_id);
    assert!(keepalive.ttl > 0, "restarted server should recover lease state");
}

#[tokio::test]
async fn slave_syncs_leases_from_backup_watch() {
    let (backup_addr, _backup_h) = start_server().await;

    let mut slave_cfg = MetaStoreServerConfig::default();
    slave_cfg.role = yr_metastore_server::MetaStoreRole::Slave;
    slave_cfg.etcd_endpoints = vec![etcd_endpoint(backup_addr)];
    slave_cfg.lease_watch_idle_resync_secs = 2;
    let (slave_addr, _slave_h) = start_server_with_config(slave_cfg.clone()).await;

    let backup = etcd_client::Client::connect([etcd_endpoint(backup_addr)], None)
        .await
        .expect("connect backup etcd");
    let lease_id = 4242_i64;
    let backup_key = format!(
        "{}/{}",
        slave_cfg.lease_backup_prefix.trim_end_matches('/'),
        lease_id
    );
    backup
        .kv_client()
        .put(
            backup_key.as_bytes(),
            format!(r#"{{"id":{lease_id},"ttl":30}}"#).into_bytes(),
            None,
        )
        .await
        .expect("put lease backup");

    let mut slave = grpc_client(slave_addr).await;
    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let mut synced = false;
    while tokio::time::Instant::now() < deadline {
        let keepalive = slave
            .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
            .await
            .expect("keep alive once")
            .into_inner();
        if keepalive.ttl > 0 {
            synced = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(synced, "slave should observe lease backup puts after startup");

    backup
        .kv_client()
        .delete(backup_key.as_bytes(), None)
        .await
        .expect("delete lease backup");
    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let mut deleted = false;
    while tokio::time::Instant::now() < deadline {
        let keepalive = slave
            .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
            .await
            .expect("keep alive once after delete")
            .into_inner();
        if keepalive.ttl < 0 {
            deleted = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(deleted, "slave should observe lease backup deletes after startup");
}

#[tokio::test]
async fn slave_resyncs_leases_after_backup_restart() {
    let probe = TcpListener::bind("127.0.0.1:0").await.expect("bind probe");
    let backup_addr = probe.local_addr().expect("probe addr");
    drop(probe);

    let (_backup_addr, backup_h) =
        start_server_at_addr_with_config(backup_addr, MetaStoreServerConfig::default()).await;

    let mut slave_cfg = MetaStoreServerConfig::default();
    slave_cfg.role = yr_metastore_server::MetaStoreRole::Slave;
    slave_cfg.etcd_endpoints = vec![etcd_endpoint(backup_addr)];
    slave_cfg.lease_watch_idle_resync_secs = 2;
    let (slave_addr, _slave_h) = start_server_with_config(slave_cfg.clone()).await;

    backup_h.abort();
    tokio::time::sleep(Duration::from_millis(100)).await;

    let (_replacement_addr, _replacement_h) =
        start_server_at_addr_with_config(backup_addr, MetaStoreServerConfig::default()).await;
    let backup = etcd_client::Client::connect([etcd_endpoint(backup_addr)], None)
        .await
        .expect("connect replacement backup");
    let lease_id = 9898_i64;
    let backup_key = format!(
        "{}/{}",
        slave_cfg.lease_backup_prefix.trim_end_matches('/'),
        lease_id
    );
    backup
        .kv_client()
        .put(
            backup_key.as_bytes(),
            format!(r#"{{"id":{lease_id},"ttl":30}}"#).into_bytes(),
            None,
        )
        .await
        .expect("put replacement lease");

    let mut slave = grpc_client(slave_addr).await;
    let deadline = tokio::time::Instant::now() + Duration::from_secs(5);
    let mut recovered = false;
    while tokio::time::Instant::now() < deadline {
        let keepalive = slave
            .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
            .await
            .expect("keep alive once after backup restart")
            .into_inner();
        if keepalive.ttl > 0 {
            recovered = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(
        recovered,
        "slave should reconnect and resync lease state after backup restart"
    );
}

#[tokio::test]
async fn slave_clears_stale_leases_after_empty_backup_resync() {
    let probe = TcpListener::bind("127.0.0.1:0").await.expect("bind probe");
    let backup_addr = probe.local_addr().expect("probe addr");
    drop(probe);

    let (_backup_addr, backup_h) =
        start_server_at_addr_with_config(backup_addr, MetaStoreServerConfig::default()).await;

    let mut slave_cfg = MetaStoreServerConfig::default();
    slave_cfg.role = yr_metastore_server::MetaStoreRole::Slave;
    slave_cfg.etcd_endpoints = vec![etcd_endpoint(backup_addr)];
    slave_cfg.lease_watch_idle_resync_secs = 2;
    let (slave_addr, _slave_h) = start_server_with_config(slave_cfg.clone()).await;

    let backup = etcd_client::Client::connect([etcd_endpoint(backup_addr)], None)
        .await
        .expect("connect backup");
    let lease_id = 31337_i64;
    let backup_key = format!(
        "{}/{}",
        slave_cfg.lease_backup_prefix.trim_end_matches('/'),
        lease_id
    );
    backup
        .kv_client()
        .put(
            backup_key.as_bytes(),
            format!(r#"{{"id":{lease_id},"ttl":30}}"#).into_bytes(),
            None,
        )
        .await
        .expect("put lease");

    let mut slave = grpc_client(slave_addr).await;
    let deadline = tokio::time::Instant::now() + Duration::from_secs(3);
    let mut synced = false;
    while tokio::time::Instant::now() < deadline {
        let keepalive = slave
            .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
            .await
            .expect("keep alive once after initial sync")
            .into_inner();
        if keepalive.ttl > 0 {
            synced = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(synced, "slave should first learn the backup lease");

    backup_h.abort();
    tokio::time::sleep(Duration::from_millis(100)).await;

    let (_replacement_addr, _replacement_h) =
        start_server_at_addr_with_config(backup_addr, MetaStoreServerConfig::default()).await;

    let deadline = tokio::time::Instant::now() + Duration::from_secs(5);
    let mut cleared = false;
    while tokio::time::Instant::now() < deadline {
        let keepalive = slave
            .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
            .await
            .expect("keep alive once after empty resync")
            .into_inner();
        if keepalive.ttl < 0 {
            cleared = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    assert!(
        cleared,
        "slave should drop stale leases when backup resync snapshot is empty"
    );
}
