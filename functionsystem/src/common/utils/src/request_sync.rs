//! Tokio port of `request_sync_helper.h` patterns (no LiteBus).

use crate::status::{Status, StatusCode};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{oneshot, Mutex};
use tokio::task::JoinHandle;
use tokio::time::{sleep, Duration};

struct Pending<R> {
    tx: oneshot::Sender<Option<R>>,
    timeout_task: JoinHandle<()>,
}

/// Correlate async replies by key; [`oneshot::Receiver`] yields `None` on timeout.
pub struct RequestSyncHelper<R> {
    inner: Arc<Mutex<HashMap<String, Pending<R>>>>,
}

impl<R: Send + 'static> RequestSyncHelper<R> {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Starts waiting for `key`; aborts any previous wait on the same key.
    pub async fn add_synchronizer(&self, key: String, timeout: Duration) -> oneshot::Receiver<Option<R>> {
        let mut map = self.inner.lock().await;
        if let Some(old) = map.remove(&key) {
            old.timeout_task.abort();
        }
        let (tx, rx) = oneshot::channel();
        let inner = Arc::clone(&self.inner);
        let k = key.clone();
        let timeout_task = tokio::spawn(async move {
            sleep(timeout).await;
            let mut m = inner.lock().await;
            if let Some(p) = m.remove(&k) {
                let _ = p.tx.send(None);
            }
        });
        map.insert(
            key,
            Pending {
                tx,
                timeout_task,
            },
        );
        rx
    }

    /// Completes a pending synchronizer; aborts its timeout timer.
    pub async fn synchronized(&self, key: &str, rsp: R) -> Status {
        let mut map = self.inner.lock().await;
        let Some(p) = map.remove(key) else {
            return Status::new(StatusCode::Failed, "unknown sync key");
        };
        p.timeout_task.abort();
        let _ = p.tx.send(Some(rsp));
        Status::ok()
    }

    /// Same as a timer firing with `REQUEST_TIME_OUT`.
    pub async fn request_timeout(&self, key: &str) {
        let mut map = self.inner.lock().await;
        if let Some(p) = map.remove(key) {
            p.timeout_task.abort();
            let _ = p.tx.send(None);
        }
    }
}

/// Tracks in-flight keyed retries with configurable backoff (state only; caller sends messages).
pub struct BackOffRetryHelper<R> {
    pending: Mutex<HashMap<String, oneshot::Sender<R>>>,
    backoff_ms: Box<dyn Fn(i64) -> u64 + Send + Sync>,
    attempt_limit: i64,
}

impl<R: Send + 'static> BackOffRetryHelper<R> {
    pub fn new<F>(backoff_ms: F, attempt_limit: i64) -> Self
    where
        F: Fn(i64) -> u64 + Send + Sync + 'static,
    {
        Self {
            pending: Mutex::new(HashMap::new()),
            backoff_ms: Box::new(backoff_ms),
            attempt_limit,
        }
    }

    pub async fn exist(&self, key: &str) -> bool {
        self.pending.lock().await.contains_key(key)
    }

    pub async fn begin(&self, key: String) -> oneshot::Receiver<R> {
        let (tx, rx) = oneshot::channel();
        let mut m = self.pending.lock().await;
        m.insert(key, tx);
        rx
    }

    pub async fn end(&self, key: &str, rsp: R) {
        let mut m = self.pending.lock().await;
        if let Some(tx) = m.remove(key) {
            let _ = tx.send(rsp);
        }
    }

    pub fn backoff_delay_ms(&self, attempt: i64) -> u64 {
        (self.backoff_ms)(attempt)
    }

    pub fn exceed_attempt_limit(&self, attempt: i64) -> bool {
        if self.attempt_limit == -1 {
            return false;
        }
        attempt > self.attempt_limit
    }

    pub async fn failed(&self, key: &str) {
        let mut m = self.pending.lock().await;
        m.remove(key);
    }

    pub async fn end_all(&self, rsp: R)
    where
        R: Clone,
    {
        let mut m = self.pending.lock().await;
        for (_, tx) in m.drain() {
            let _ = tx.send(rsp.clone());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn request_sync_complete_before_timeout() {
        let h: RequestSyncHelper<i32> = RequestSyncHelper::new();
        let rx = h
            .add_synchronizer("k1".into(), Duration::from_secs(10))
            .await;
        let st = h.synchronized("k1", 42).await;
        assert!(st.is_ok());
        assert_eq!(rx.await.unwrap(), Some(42));
    }

    #[tokio::test]
    async fn request_sync_times_out() {
        let h: RequestSyncHelper<i32> = RequestSyncHelper::new();
        let rx = h
            .add_synchronizer("k1".into(), Duration::from_millis(30))
            .await;
        assert_eq!(rx.await.unwrap(), None);
    }

    #[tokio::test]
    async fn backoff_end_and_limit() {
        let h: BackOffRetryHelper<&'static str> =
            BackOffRetryHelper::new(|a| (a * 10) as u64, 3);
        assert_eq!(h.backoff_delay_ms(2), 20);
        assert!(!h.exceed_attempt_limit(3));
        assert!(h.exceed_attempt_limit(4));
        let rx = h.begin("x".into()).await;
        h.end("x", "done").await;
        assert_eq!(rx.await.unwrap(), "done");
    }
}
