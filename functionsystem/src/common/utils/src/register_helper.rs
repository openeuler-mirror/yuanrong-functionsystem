//! Registration helper with retries — port of `common/register/register_helper*.h` (litebus-free, tokio-based).

use crate::constants::register;
use crate::heartbeat::{
    HeartbeatClientDriver, HeartbeatClientTimeoutHandler, HeartbeatObserveDriver,
    HeartbeatObserverTimeoutHandler, HeartbeatPeer,
};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

type RegisterMsgCb = Arc<dyn Fn(String) + Send + Sync>;
type RegisteredCb = Arc<dyn Fn(String) + Send + Sync>;
type TimeoutCb = Arc<dyn Fn() + Send + Sync>;

/// Downstream / upstream registration handshake with timed retries (mirrors `RegisterHelperActor`).
pub struct RegisterHelper {
    name: String,
    register_interval: Duration,
    registered: Arc<AtomicBool>,
    register_cb: Mutex<Option<RegisterMsgCb>>,
    registered_cb: Mutex<Option<RegisteredCb>>,
    timeout_cb: Mutex<Option<TimeoutCb>>,
    retry_task: Mutex<Option<JoinHandle<()>>>,
    component_name: Mutex<String>,
    ping_driver: Mutex<Option<HeartbeatClientDriver>>,
    observe_driver: Mutex<Option<HeartbeatObserveDriver>>,
}

impl RegisterHelper {
    pub fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            register_interval: Duration::from_millis(register::DEFAULT_REGISTER_TIMEOUT_MS),
            registered: Arc::new(AtomicBool::new(false)),
            register_cb: Mutex::new(None),
            registered_cb: Mutex::new(None),
            timeout_cb: Mutex::new(None),
            retry_task: Mutex::new(None),
            component_name: Mutex::new(String::new()),
            ping_driver: Mutex::new(None),
            observe_driver: Mutex::new(None),
        }
    }

    pub fn with_interval(mut self, ms: u64) -> Self {
        self.register_interval = Duration::from_millis(ms);
        self
    }

    pub fn logical_name(&self) -> &str {
        &self.name
    }

    pub async fn set_register_callback(&self, f: RegisterMsgCb) {
        *self.register_cb.lock().await = Some(f);
    }

    pub async fn set_registered_callback(&self, f: RegisteredCb) {
        *self.registered_cb.lock().await = Some(f);
    }

    pub async fn set_register_timeout_callback(&self, f: TimeoutCb) {
        *self.timeout_cb.lock().await = Some(f);
    }

    pub async fn set_component_name(&self, n: String) {
        *self.component_name.lock().await = n;
    }

    /// Same semantics as `RegisterHelperActor::StartRegister`: initial attempt is assumed done by the
    /// caller; this schedules the retry loop with `max_registers_times - 1` follow-ups after each interval.
    pub async fn start_register(
        &self,
        target_name: &str,
        address: &str,
        msg: String,
        max_registers_times: u32,
    ) {
        self.registered.store(false, Ordering::SeqCst);
        {
            let mut g = self.retry_task.lock().await;
            if let Some(h) = g.take() {
                h.abort();
            }
        }

        let target = format!("{}{}", target_name, register::REGISTER_HELPER_SUFFIX);
        let addr = address.to_string();
        let interval = self.register_interval;
        let registered = self.registered.clone();
        let timeout_cb = self.timeout_cb.lock().await.clone();
        tracing::debug!(
            target = %target,
            address = %addr,
            msg = %msg,
            "register scheduled (C++ initial Send + AsyncAfter pattern)"
        );

        let handle = tokio::spawn(async move {
            let mut retry_times = max_registers_times.saturating_sub(1);
            loop {
                tokio::time::sleep(interval).await;
                if registered.load(Ordering::SeqCst) {
                    return;
                }
                if retry_times == 0 {
                    if let Some(cb) = timeout_cb {
                        cb();
                    }
                    return;
                }
                retry_times -= 1;
                tracing::debug!(target = %target, address = %addr, retry_times, "register retry");
            }
        });

        *self.retry_task.lock().await = Some(handle);
    }

    pub async fn on_register_message(&self, msg: String) {
        if let Some(cb) = self.register_cb.lock().await.as_ref() {
            cb(msg);
        }
    }

    pub async fn on_registered_message(&self, msg: String) {
        if self.registered.swap(true, Ordering::SeqCst) {
            return;
        }
        if let Some(h) = self.retry_task.lock().await.take() {
            h.abort();
        }
        if let Some(cb) = self.registered_cb.lock().await.as_ref() {
            cb(msg);
        }
    }

    pub fn is_registered(&self) -> bool {
        self.registered.load(Ordering::SeqCst)
    }

    pub async fn set_ping_pong_driver(
        &self,
        dst_name: &str,
        dst_address: &str,
        _timeout_ms: u32,
        handler: HeartbeatClientTimeoutHandler,
        _heartbeat_name: &str,
    ) {
        let comp = self.component_name.lock().await.clone();
        let logical = format!("{}-{}", comp, dst_name);
        let peer = HeartbeatPeer {
            logical_name: logical,
            address: dst_address.to_string(),
        };
        let drv = HeartbeatClientDriver::new(peer, handler);
        drv.start().await;
        *self.ping_driver.lock().await = Some(drv);
    }

    pub async fn set_heartbeat_observe_driver(
        &self,
        dst_name: &str,
        dst_address: &str,
        timeout_ms: u32,
        handler: HeartbeatObserverTimeoutHandler,
        _heartbeat_name: &str,
    ) {
        let comp = self.component_name.lock().await.clone();
        let logical = HeartbeatPeer::client_aid_name(&comp, dst_name);
        let peer = HeartbeatPeer {
            logical_name: logical,
            address: dst_address.to_string(),
        };
        let mut od = self.observe_driver.lock().await;
        if od.is_none() {
            *od = Some(HeartbeatObserveDriver::new(
                crate::constants::heartbeat::DEFAULT_PING_PONG_TIMEOUT_MS,
            ));
        }
        if let Some(ref o) = *od {
            o.add_node(peer, timeout_ms, handler).await;
        }
    }

    pub async fn stop_ping_pong_driver(&self) {
        if let Some(d) = self.ping_driver.lock().await.take() {
            d.stop().await;
        }
    }

    pub async fn stop_heartbeat_observer(&self) {
        self.observe_driver.lock().await.take();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn register_timeout_after_retries() {
        let h = RegisterHelper::new("a").with_interval(20);
        let timed_out = Arc::new(AtomicBool::new(false));
        h.set_register_timeout_callback(Arc::new({
            let timed_out = timed_out.clone();
            move || {
                timed_out.store(true, Ordering::SeqCst);
            }
        }))
        .await;
        h.start_register("peer", "127.0.0.1:9", "m".into(), 2).await;
        tokio::time::sleep(Duration::from_millis(80)).await;
        assert!(timed_out.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn register_succeeds_before_timeout() {
        let h = RegisterHelper::new("b").with_interval(40);
        let timed_out = Arc::new(AtomicBool::new(false));
        h.set_register_timeout_callback(Arc::new({
            let timed_out = timed_out.clone();
            move || {
                timed_out.store(true, Ordering::SeqCst);
            }
        }))
        .await;
        let ok = Arc::new(AtomicBool::new(false));
        h.set_registered_callback(Arc::new({
            let ok = ok.clone();
            move |_m: String| {
                ok.store(true, Ordering::SeqCst);
            }
        }))
        .await;
        h.start_register("peer", "127.0.0.1:9", "m".into(), 10).await;
        tokio::time::sleep(Duration::from_millis(10)).await;
        h.on_registered_message("ack".into()).await;
        tokio::time::sleep(Duration::from_millis(200)).await;
        assert!(h.is_registered());
        assert!(ok.load(Ordering::SeqCst));
        assert!(!timed_out.load(Ordering::SeqCst));
    }
}
