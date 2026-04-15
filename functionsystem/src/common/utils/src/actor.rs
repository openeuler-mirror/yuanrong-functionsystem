//! Actor driver pattern — port of `common/utils/actor_driver.h` plus tokio lifecycle, supervisor, workers,
//! bounded mailboxes, and periodic memory trim (glibc `malloc_trim` analogue).

use async_trait::async_trait;
use std::future::Future;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use tokio_util::sync::CancellationToken;

use crate::constants::memory;
use crate::status::Status;

// ----------------------------------------------------------------------------- BasisActor / ActorDriver

#[derive(Debug)]
pub struct BasisActorCore {
    name: String,
    ready: Arc<AtomicBool>,
}

impl BasisActorCore {
    pub fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            ready: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn to_ready(&self) {
        tracing::info!(actor = %self.name, "actor is ready");
        self.ready.store(true, Ordering::SeqCst);
    }

    pub fn is_ready(&self) -> bool {
        self.ready.load(Ordering::SeqCst)
    }
}

#[async_trait]
pub trait BasisActor: Send + Sync {
    fn actor_name(&self) -> &str;

    async fn sync(&self) -> Status {
        Status::ok()
    }

    async fn recover(&self) -> Status {
        Status::ok()
    }

    fn to_ready(&self);

    fn is_ready(&self) -> bool;
}

#[async_trait]
impl BasisActor for BasisActorCore {
    fn actor_name(&self) -> &str {
        self.name()
    }

    fn to_ready(&self) {
        BasisActorCore::to_ready(self);
    }

    fn is_ready(&self) -> bool {
        BasisActorCore::is_ready(self)
    }
}

pub struct ActorDriver<A: BasisActor + ?Sized> {
    actor: Arc<A>,
    cancel: CancellationToken,
    join: Arc<tokio::sync::Mutex<Option<JoinHandle<()>>>>,
}

impl<A: BasisActor + ?Sized> ActorDriver<A> {
    pub fn new(actor: Arc<A>, cancel: CancellationToken, join: JoinHandle<()>) -> Self {
        Self {
            actor,
            cancel,
            join: Arc::new(tokio::sync::Mutex::new(Some(join))),
        }
    }

    pub fn cancel_token(&self) -> CancellationToken {
        self.cancel.clone()
    }

    pub async fn sync(&self) -> Status {
        self.actor.sync().await
    }

    pub async fn recover(&self) -> Status {
        self.actor.recover().await
    }

    pub fn to_ready(&self) {
        self.actor.to_ready();
    }

    pub fn is_ready(&self) -> bool {
        self.actor.is_ready()
    }

    pub fn stop(&self) {
        self.cancel.cancel();
    }

    pub async fn await_join(&self) {
        let mut g = self.join.lock().await;
        if let Some(j) = g.take() {
            let _ = j.await;
        }
    }

    pub fn actor_name(&self) -> &str {
        self.actor.actor_name()
    }
}

impl<A: BasisActor + ?Sized> Drop for ActorDriver<A> {
    fn drop(&mut self) {
        self.cancel.cancel();
    }
}

pub async fn actor_sync_all<A: BasisActor + ?Sized>(drivers: &[Arc<ActorDriver<A>>]) -> Status {
    for d in drivers {
        tracing::info!(actor = %d.actor_name(), "start to sync");
        let s = d.sync().await;
        if s.is_error() {
            tracing::error!(actor = %d.actor_name(), "failed to sync: {s}");
            return s;
        }
    }
    Status::ok()
}

pub async fn actor_recover_all<A: BasisActor + ?Sized>(drivers: &[Arc<ActorDriver<A>>]) -> Status {
    for d in drivers {
        tracing::info!(actor = %d.actor_name(), "start to recover");
        let s = d.recover().await;
        if s.is_error() {
            tracing::error!(actor = %d.actor_name(), "failed to recover: {s}");
            return s;
        }
    }
    Status::ok()
}

pub fn actor_ready_all<A: BasisActor + ?Sized>(drivers: &[Arc<ActorDriver<A>>]) {
    for d in drivers {
        d.to_ready();
    }
}

/// Runs `sync → recover → run` until `cancel` is triggered.
pub fn spawn_basis_driver<F, Fut>(
    name: impl Into<String>,
    run: F,
) -> Arc<ActorDriver<BasisActorCore>>
where
    F: FnOnce(Arc<BasisActorCore>, CancellationToken) -> Fut + Send + 'static,
    Fut: Future<Output = ()> + Send + 'static,
{
    let core = Arc::new(BasisActorCore::new(name));
    let cancel = CancellationToken::new();
    let c2 = cancel.clone();
    let c_core = core.clone();
    let join = tokio::spawn(async move {
        if c_core.sync().await.is_error() {
            return;
        }
        if c_core.recover().await.is_error() {
            return;
        }
        run(c_core, c2).await;
    });
    Arc::new(ActorDriver::new(core, cancel, join))
}

// ----------------------------------------------------------------------------- Message actors (bounded / unbounded)

#[derive(Debug)]
pub enum ActorSendError<M> {
    Bounded(mpsc::error::SendError<M>),
    Unbounded(mpsc::error::SendError<M>),
}

pub enum ActorTx<M: Send + 'static> {
    Bounded(mpsc::Sender<M>),
    Unbounded(mpsc::UnboundedSender<M>),
}

impl<M: Send + 'static> ActorTx<M> {
    pub async fn send(&self, msg: M) -> Result<(), ActorSendError<M>> {
        match self {
            ActorTx::Bounded(tx) => tx.send(msg).await.map_err(ActorSendError::Bounded),
            ActorTx::Unbounded(tx) => tx.send(msg).map_err(ActorSendError::Unbounded),
        }
    }
}

pub struct ActorHandle<M: Send + 'static> {
    pub tx: ActorTx<M>,
    pub cancel: CancellationToken,
    pub join: JoinHandle<()>,
    pub name: String,
}

impl<M: Send + 'static> ActorHandle<M> {
    pub async fn send(&self, msg: M) -> Result<(), ActorSendError<M>> {
        self.tx.send(msg).await
    }

    pub async fn stop(self) {
        self.cancel.cancel();
        let _ = self.join.await;
    }

    pub fn is_running(&self) -> bool {
        !self.join.is_finished()
    }
}

impl<M: Send + 'static> std::fmt::Debug for ActorHandle<M> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ActorHandle")
            .field("name", &self.name)
            .field("running", &self.is_running())
            .finish()
    }
}

pub struct SpawnActorConfig {
    pub name: String,
    pub capacity: Option<usize>,
}

impl SpawnActorConfig {
    pub fn unbounded(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            capacity: None,
        }
    }

    pub fn bounded(name: impl Into<String>, cap: usize) -> Self {
        Self {
            name: name.into(),
            capacity: Some(cap),
        }
    }
}

pub fn spawn_actor_with_config<M, F, Fut>(cfg: SpawnActorConfig, handler: F) -> ActorHandle<M>
where
    M: Send + 'static,
    F: FnOnce(ActorRx<M>, CancellationToken) -> Fut + Send + 'static,
    Fut: Future<Output = ()> + Send + 'static,
{
    let cancel = CancellationToken::new();
    let c2 = cancel.clone();
    let name = cfg.name.clone();
    let task_name = name.clone();

    if let Some(cap) = cfg.capacity {
        let (tx, rx) = mpsc::channel(cap);
        let join = tokio::spawn(async move {
            tracing::debug!(actor = %task_name, "bounded actor started");
            handler(ActorRx::Bounded(rx), c2).await;
            tracing::debug!(actor = %task_name, "bounded actor stopped");
        });
        ActorHandle {
            tx: ActorTx::Bounded(tx),
            cancel,
            join,
            name,
        }
    } else {
        let (tx, rx) = mpsc::unbounded_channel();
        let join = tokio::spawn(async move {
            tracing::debug!(actor = %task_name, "actor started");
            handler(ActorRx::Unbounded(rx), c2).await;
            tracing::debug!(actor = %task_name, "actor stopped");
        });
        ActorHandle {
            tx: ActorTx::Unbounded(tx),
            cancel,
            join,
            name,
        }
    }
}

pub enum ActorRx<M: Send + 'static> {
    Bounded(mpsc::Receiver<M>),
    Unbounded(mpsc::UnboundedReceiver<M>),
}

impl<M: Send + 'static> ActorRx<M> {
    pub async fn recv(&mut self) -> Option<M> {
        match self {
            ActorRx::Bounded(rx) => rx.recv().await,
            ActorRx::Unbounded(rx) => rx.recv().await,
        }
    }
}

/// Legacy unbounded actor (back-compat).
pub fn spawn_actor<M, F, Fut>(name: impl Into<String>, handler: F) -> ActorHandle<M>
where
    M: Send + 'static,
    F: FnOnce(mpsc::UnboundedReceiver<M>, CancellationToken) -> Fut + Send + 'static,
    Fut: Future<Output = ()> + Send + 'static,
{
    spawn_actor_with_config(SpawnActorConfig::unbounded(name), |rx, c| async move {
        let rx = match rx {
            ActorRx::Unbounded(r) => r,
            _ => unreachable!(),
        };
        handler(rx, c).await
    })
}

// ----------------------------------------------------------------------------- Worker / ActorWorker

pub type Worker = JoinHandle<()>;
pub type ActorWorker = JoinHandle<()>;

pub fn spawn_worker<Fut>(name: &str, fut: Fut) -> JoinHandle<()>
where
    Fut: Future<Output = ()> + Send + 'static,
{
    let n = name.to_string();
    tokio::spawn(async move {
        tracing::debug!(worker = %n, "worker started");
        fut.await;
        tracing::debug!(worker = %n, "worker finished");
    })
}

// ----------------------------------------------------------------------------- Supervisor

#[derive(Clone, Debug)]
pub struct SupervisorConfig {
    pub restart_on_panic: bool,
    pub name: String,
}

impl Default for SupervisorConfig {
    fn default() -> Self {
        Self {
            restart_on_panic: true,
            name: "supervised".into(),
        }
    }
}

/// If the inner task panics, logs and optionally respawns until it completes normally.
pub fn spawn_supervised<F, Fut>(cfg: SupervisorConfig, mut factory: F) -> JoinHandle<()>
where
    F: FnMut() -> Fut + Send + 'static,
    Fut: Future<Output = ()> + Send + 'static,
{
    let name = cfg.name.clone();
    tokio::spawn(async move {
        let mut restarts = 0u32;
        loop {
            let fut = factory();
            let j = tokio::spawn(fut);
            match j.await {
                Ok(()) => break,
                Err(e) => {
                    if e.is_panic() {
                        tracing::error!(
                            target = "yr_common::actor",
                            actor = %name,
                            restarts,
                            "actor task panicked"
                        );
                        if cfg.restart_on_panic {
                            restarts += 1;
                            continue;
                        }
                    }
                    break;
                }
            }
        }
    })
}

// ----------------------------------------------------------------------------- MemoryOptimizer

/// Periodic best-effort return of free heap pages to the OS (Linux `malloc_trim`).
pub struct MemoryOptimizer {
    cancel: CancellationToken,
    join: JoinHandle<()>,
}

impl MemoryOptimizer {
    pub fn new() -> Self {
        let cancel = CancellationToken::new();
        let c = cancel.clone();
        let join = tokio::spawn(async move {
            let mut tick = tokio::time::interval(Duration::from_millis(
                memory::DEFAULT_MEMORY_TRIM_INTERVAL_MS,
            ));
            loop {
                tokio::select! {
                    _ = c.cancelled() => break,
                    _ = tick.tick() => {
                        trim_heap();
                    }
                }
            }
        });
        Self { cancel, join }
    }

    pub fn start_trimming(self) {
        tracing::info!(
            arena_max = memory::DEFAULT_MAX_ARENA_NUM,
            interval_ms = memory::DEFAULT_MEMORY_TRIM_INTERVAL_MS,
            "memory optimizer active (malloc_trim)"
        );
        // M_ARENA_MAX has no direct stable Rust equivalent; document as no-op for now.
    }

    pub async fn shutdown(self) {
        self.cancel.cancel();
        let _ = self.join.await;
    }
}

impl Default for MemoryOptimizer {
    fn default() -> Self {
        Self::new()
    }
}

fn trim_heap() {
    #[cfg(all(unix, target_env = "gnu"))]
    unsafe {
        libc::malloc_trim(0);
    }
    #[cfg(not(all(unix, target_env = "gnu")))]
    let _ = ();
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering as AOrd};

    #[tokio::test]
    async fn basis_lifecycle_sync_recover_ready_stop() {
        let d = spawn_basis_driver("lc", |_core, cancel| async move {
            loop {
                tokio::select! {
                    _ = cancel.cancelled() => break,
                    _ = tokio::time::sleep(Duration::from_millis(20)) => {}
                }
            }
        });
        assert!(d.sync().await.is_ok());
        assert!(d.recover().await.is_ok());
        assert!(!d.is_ready());
        d.to_ready();
        assert!(d.is_ready());
        d.stop();
        d.await_join().await;
    }

    #[tokio::test]
    async fn supervisor_restarts_on_panic() {
        let count = Arc::new(AtomicUsize::new(0));
        let cfg = SupervisorConfig {
            restart_on_panic: true,
            name: "panic-test".into(),
        };
        let c = count.clone();
        let h = spawn_supervised(cfg, move || {
            let c = c.clone();
            async move {
                let n = c.fetch_add(1, AOrd::SeqCst);
                if n == 0 {
                    panic!("boom");
                }
            }
        });
        let _ = h.await;
        assert!(count.load(AOrd::SeqCst) >= 2);
    }

    #[tokio::test]
    async fn bounded_actor_backpressure() {
        let h = spawn_actor_with_config(SpawnActorConfig::bounded("bq", 1), |mut rx, cancel| async move {
            loop {
                tokio::select! {
                    _ = cancel.cancelled() => break,
                    m = rx.recv() => {
                        if m.is_none() { break; }
                        tokio::time::sleep(Duration::from_millis(200)).await;
                    }
                }
            }
        });
        let ActorTx::Bounded(tx) = &h.tx else {
            panic!("expected bounded");
        };
        assert!(tx.try_send(1u8).is_ok());
        assert!(matches!(
            tx.try_send(2u8),
            Err(mpsc::error::TrySendError::Full(_))
        ));
        h.cancel.cancel();
        h.stop().await;
    }

    #[tokio::test]
    async fn legacy_spawn_actor() {
        let h = spawn_actor::<&'static str, _, _>("echo", |mut rx, cancel| async move {
            while let Some(msg) = rx.recv().await {
                if msg == "stop" {
                    break;
                }
            }
            drop(cancel);
        });
        h.send("hello").await.unwrap();
        h.send("stop").await.unwrap();
        h.stop().await;
    }
}
