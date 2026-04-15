//! Lightweight file watcher using `notify` + Tokio channels.

use notify::{Config, Error as NotifyError, Event, RecommendedWatcher, RecursiveMode, Watcher};
use std::path::Path;
use tokio::sync::mpsc;

/// Owns a [`RecommendedWatcher`] so it stays alive while events stream.
pub struct FileMonitor {
    _watcher: RecommendedWatcher,
}

impl FileMonitor {
    /// Watch `path` (non-recursive). Events are delivered on `tx`.
    pub fn watch(
        path: impl AsRef<Path>,
        tx: mpsc::UnboundedSender<Result<Event, NotifyError>>,
    ) -> Result<Self, NotifyError> {
        let path = path.as_ref().to_path_buf();
        let mut watcher = RecommendedWatcher::new(
            move |res| {
                let _ = tx.send(res);
            },
            Config::default(),
        )?;
        watcher.watch(&path, RecursiveMode::NonRecursive)?;
        Ok(Self { _watcher: watcher })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    use tokio::time::timeout;

    #[tokio::test]
    async fn watch_emits_on_write() {
        let dir = std::env::temp_dir().join(format!("yr_fm_{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&dir).unwrap();
        let (tx, mut rx) = mpsc::unbounded_channel();
        let _mon = FileMonitor::watch(&dir, tx).expect("watch");
        let f = dir.join("probe.txt");
        tokio::task::spawn_blocking({
            let f = f.clone();
            move || {
                std::fs::write(&f, b"x").unwrap();
            }
        })
        .await
        .unwrap();
        let got = timeout(Duration::from_secs(3), rx.recv())
            .await
            .expect("timeout")
            .expect("channel");
        assert!(got.is_ok());
    }
}
