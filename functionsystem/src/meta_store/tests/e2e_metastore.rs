//! E2E: embedded MetaStore server with etcd-compatible gRPC (KV, range, watch, lease, txn).

use std::time::Duration;

use etcd_client::{
    Compare, CompareOp, EventType, GetOptions, PutOptions, Txn as EtcdTxn, TxnOp, WatchOptions,
};
use tokio::net::TcpListener;
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};
use yr_metastore_server::{MetaStoreServer, MetaStoreServerConfig};

async fn start_server() -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr = listener.local_addr().expect("addr");
    let mut cfg = MetaStoreServerConfig::default();
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

#[tokio::test]
async fn kv_put_get_delete_round_trip() {
    let (addr, _h) = start_server().await;
    let mut etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
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
    let mut etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
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
    let mut etcd = etcd_client::Client::connect([ep.clone()], None)
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
        let mut c = etcd_client::Client::connect([ep], None).await.unwrap();
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
    let mut etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
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
    let mut etcd = etcd_client::Client::connect([etcd_endpoint(addr)], None)
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
