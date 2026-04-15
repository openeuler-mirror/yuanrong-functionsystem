//! Heartbeat client / observer — port of `common/heartbeat/heartbeat_client.h` and `heartbeat_observer.h`.

use crate::constants::{actor_name, heartbeat as hb};
use crate::status::Status;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{Mutex, RwLock};
use tokio::task::AbortHandle;
use tokio::time::{Duration, Instant};

/// `HeartbeatConnection` from `heartbeat_observer.h`
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HeartbeatConnection {
    Lost = 0,
    Exited = 1,
}

pub type HeartbeatClientTimeoutHandler = Arc<dyn Fn(HeartbeatPeer) + Send + Sync>;
pub type HeartbeatObserverTimeoutHandler =
    Arc<dyn Fn(HeartbeatPeer, HeartbeatConnection) + Send + Sync>;

#[derive(Debug, Clone, Hash, PartialEq, Eq)]
pub struct HeartbeatPeer {
    pub logical_name: String,
    pub address: String,
}

impl HeartbeatPeer {
    pub fn client_aid_name(component_prefix: &str, dst_name: &str) -> String {
        format!(
            "{}{}{}{}",
            actor_name::HEARTBEAT_CLIENT_BASENAME,
            component_prefix,
            dst_name,
            crate::constants::register::REGISTER_HELPER_SUFFIX
        )
    }

    pub fn observer_aid_name(heartbeat_name: &str) -> String {
        format!("{}{}", actor_name::HEARTBEAT_OBSERVER_BASENAME, heartbeat_name)
    }
}

/// Tracks remote liveness: if `record_pong` is not called within `timeout`, the handler fires once.
pub struct HeartbeatObserver {
    nodes: Arc<RwLock<HashMap<String, Node>>>,
}

struct Node {
    timeout: Duration,
    handler: HeartbeatObserverTimeoutHandler,
    abort: AbortHandle,
}

impl HeartbeatObserver {
    pub fn new(_default_timeout_ms: u32) -> Self {
        Self {
            nodes: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    pub fn with_default_timeout() -> Self {
        Self::new(hb::DEFAULT_PING_PONG_TIMEOUT_MS)
    }

    fn key(peer: &HeartbeatPeer) -> String {
        format!("{}@{}", peer.logical_name, peer.address)
    }

    pub async fn add_node(
        &self,
        peer: HeartbeatPeer,
        timeout_ms: u32,
        handler: HeartbeatObserverTimeoutHandler,
    ) {
        self.remove_node(&peer).await;
        let timeout = Duration::from_millis(timeout_ms as u64);
        let nodes = self.nodes.clone();
        let peer_clone = peer.clone();
        let h = handler.clone();
        let abort = Self::spawn_watchdog(nodes, peer_clone, timeout, h).await;
        let node = Node {
            timeout,
            handler,
            abort,
        };
        self.nodes.write().await.insert(Self::key(&peer), node);
    }

    pub async fn remove_node(&self, peer: &HeartbeatPeer) {
        if let Some(n) = self.nodes.write().await.remove(&Self::key(peer)) {
            n.abort.abort();
        }
    }

    pub async fn reset_node(
        &self,
        peer: HeartbeatPeer,
        timeout_ms: u32,
        handler: HeartbeatObserverTimeoutHandler,
    ) {
        self.add_node(peer, timeout_ms, handler).await;
    }

    async fn spawn_watchdog(
        nodes: Arc<RwLock<HashMap<String, Node>>>,
        peer: HeartbeatPeer,
        timeout: Duration,
        handler: HeartbeatObserverTimeoutHandler,
    ) -> AbortHandle {
        let key = Self::key(&peer);
        let h = tokio::spawn(async move {
            tokio::time::sleep_until(Instant::now() + timeout).await;
            handler(peer.clone(), HeartbeatConnection::Lost);
            let _ = nodes.write().await.remove(&key);
        });
        h.abort_handle()
    }

    /// Called when a ping (or pong) is observed from `peer`; resets the watchdog.
    pub async fn record_activity(&self, peer: &HeartbeatPeer) {
        let node_opt = self.nodes.write().await.remove(&Self::key(peer));
        if let Some(n) = node_opt {
            n.abort.abort();
            let timeout = n.timeout;
            let handler = n.handler.clone();
            let abort = Self::spawn_watchdog(self.nodes.clone(), peer.clone(), timeout, handler.clone()).await;
            self.nodes.write().await.insert(
                Self::key(peer),
                Node {
                    timeout,
                    handler,
                    abort,
                },
            );
        }
    }
}

/// Owns a background task that runs [`HeartbeatObserver`].
#[derive(Clone)]
pub struct HeartbeatObserveDriver {
    inner: Arc<HeartbeatObserver>,
}

impl HeartbeatObserveDriver {
    pub fn new(default_timeout_ms: u32) -> Self {
        Self {
            inner: Arc::new(HeartbeatObserver::new(default_timeout_ms)),
        }
    }

    pub async fn add_node(
        &self,
        peer: HeartbeatPeer,
        timeout_ms: u32,
        handler: HeartbeatObserverTimeoutHandler,
    ) {
        self.inner.add_node(peer, timeout_ms, handler).await;
    }

    pub async fn remove_node(&self, peer: &HeartbeatPeer) {
        self.inner.remove_node(peer).await;
    }

    pub async fn record_activity(&self, peer: &HeartbeatPeer) {
        self.inner.record_activity(peer).await;
    }
}

/// Outbound heartbeat: periodically expects `record_pong` or the timeout handler runs.
pub struct HeartbeatClient {
    peer: HeartbeatPeer,
    max_missed: u32,
    cycle: Duration,
    handler: HeartbeatClientTimeoutHandler,
    state: Arc<Mutex<HeartbeatClientState>>,
}

struct HeartbeatClientState {
    missed: u32,
    running: bool,
    abort: Option<AbortHandle>,
}

impl HeartbeatClient {
    pub fn new(
        peer: HeartbeatPeer,
        max_missed: u32,
        cycle_ms: u32,
        handler: HeartbeatClientTimeoutHandler,
    ) -> Self {
        Self {
            peer,
            max_missed,
            cycle: Duration::from_millis(cycle_ms as u64),
            handler,
            state: Arc::new(Mutex::new(HeartbeatClientState {
                missed: 0,
                running: false,
                abort: None,
            })),
        }
    }

    pub fn with_defaults(peer: HeartbeatPeer, handler: HeartbeatClientTimeoutHandler) -> Self {
        Self::new(
            peer,
            hb::DEFAULT_PING_NUMS,
            hb::DEFAULT_PING_CYCLE_MS,
            handler,
        )
    }

    pub async fn start(&self) {
        let mut g = self.state.lock().await;
        if g.running {
            return;
        }
        g.running = true;
        g.missed = 0;
        let state = self.state.clone();
        let peer = self.peer.clone();
        let handler = self.handler.clone();
        let cycle = self.cycle;
        let max = self.max_missed;
        let h = tokio::spawn(async move {
            loop {
                tokio::time::sleep(cycle).await;
                let mut g = state.lock().await;
                if !g.running {
                    break;
                }
                g.missed += 1;
                if g.missed >= max {
                    g.running = false;
                    handler(peer.clone());
                    break;
                }
            }
        });
        g.abort = Some(h.abort_handle());
    }

    pub async fn stop(&self) {
        let mut g = self.state.lock().await;
        g.running = false;
        if let Some(a) = g.abort.take() {
            a.abort();
        }
        g.missed = 0;
    }

    pub async fn record_pong(&self) {
        let mut g = self.state.lock().await;
        g.missed = 0;
    }

    pub fn status(&self) -> Status {
        // Minimal port: running without timeout is OK.
        Status::ok()
    }
}

pub struct HeartbeatClientDriver {
    inner: Arc<Mutex<HeartbeatClient>>,
}

impl HeartbeatClientDriver {
    pub fn new(peer: HeartbeatPeer, handler: HeartbeatClientTimeoutHandler) -> Self {
        Self {
            inner: Arc::new(Mutex::new(HeartbeatClient::with_defaults(peer, handler))),
        }
    }

    pub async fn start(&self) {
        self.inner.lock().await.start().await;
    }

    pub async fn stop(&self) {
        self.inner.lock().await.stop().await;
    }

    pub async fn record_pong(&self) {
        self.inner.lock().await.record_pong().await;
    }

    pub async fn get_status(&self) -> Status {
        self.inner.lock().await.status()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    #[tokio::test]
    async fn observer_fires_on_timeout() {
        let obs = HeartbeatObserveDriver::new(5000);
        let hits = Arc::new(AtomicU32::new(0));
        let h = {
            let hits = hits.clone();
            Arc::new(move |_p: HeartbeatPeer, c: HeartbeatConnection| {
                assert_eq!(c, HeartbeatConnection::Lost);
                hits.fetch_add(1, Ordering::SeqCst);
            }) as HeartbeatObserverTimeoutHandler
        };
        let peer = HeartbeatPeer {
            logical_name: "a".into(),
            address: "127.0.0.1:1".into(),
        };
        obs.add_node(peer.clone(), 40, h).await;
        tokio::time::sleep(Duration::from_millis(120)).await;
        assert!(hits.load(Ordering::SeqCst) >= 1);
    }

    #[tokio::test]
    async fn observer_reset_by_activity() {
        let obs = HeartbeatObserveDriver::new(5000);
        let hits = Arc::new(AtomicU32::new(0));
        let h = {
            let hits = hits.clone();
            Arc::new(move |_p: HeartbeatPeer, _c: HeartbeatConnection| {
                hits.fetch_add(1, Ordering::SeqCst);
            }) as HeartbeatObserverTimeoutHandler
        };
        let peer = HeartbeatPeer {
            logical_name: "b".into(),
            address: "127.0.0.1:2".into(),
        };
        obs.add_node(peer.clone(), 200, h).await;
        for _ in 0..10 {
            tokio::time::sleep(Duration::from_millis(20)).await;
            obs.record_activity(&peer).await;
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
        assert_eq!(hits.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn client_timeout_after_missed_cycles() {
        let peer = HeartbeatPeer {
            logical_name: "c".into(),
            address: "x".into(),
        };
        let hits = Arc::new(AtomicU32::new(0));
        let h = {
            let hits = hits.clone();
            Arc::new(move |_p: HeartbeatPeer| {
                hits.fetch_add(1, Ordering::SeqCst);
            }) as HeartbeatClientTimeoutHandler
        };
        let cli = HeartbeatClient::new(peer, 3, 20, h);
        cli.start().await;
        tokio::time::sleep(Duration::from_millis(100)).await;
        assert!(hits.load(Ordering::SeqCst) >= 1);
    }
}
