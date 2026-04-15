//! etcd watch on new-token prefix: triggers rotation pass when peers update tokens.

use std::sync::Arc;
use std::time::Duration;

use futures::StreamExt;
use tracing::warn;
use yr_metastore_client::WatchEventType;

use crate::state::AppState;
use crate::token::TokenManager;

pub async fn run(state: Arc<AppState>) {
    loop {
        let Some(ms) = state.metastore.clone() else {
            return;
        };
        let prefix = TokenManager::etcd_watch_prefix_new_tokens(&state.config);
        let mut stream = {
            let mut g = ms.lock().await;
            g.watch_prefix(&prefix)
        };
        while let Some(ev) = stream.next().await {
            match ev {
                Ok(we)
                    if matches!(
                        we.event_type,
                        WatchEventType::Put | WatchEventType::Delete
                    ) =>
                {
                    let st = state.clone();
                    tokio::spawn(async move {
                        tokio::time::sleep(Duration::from_millis(400)).await;
                        let Some(inner) = st.metastore.as_ref() else {
                            return;
                        };
                        let mut g = inner.lock().await;
                        TokenManager::rotation_tick(&mut g, &st.config).await;
                    });
                }
                Ok(_) => {}
                Err(e) => {
                    warn!(error = %e, "token watch stream error; reconnecting");
                    break;
                }
            }
        }
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
}
