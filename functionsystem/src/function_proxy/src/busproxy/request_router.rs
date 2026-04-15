//! Low-reliability retry routing for forward operations when peer metadata is briefly missing.

use std::time::Duration;
use tokio::time::sleep;

pub struct RouteRetry {
    pub max_attempts: u32,
    pub base_delay: Duration,
}

impl Default for RouteRetry {
    fn default() -> Self {
        Self {
            max_attempts: 5,
            base_delay: Duration::from_millis(50),
        }
    }
}

impl RouteRetry {
    pub async fn backoff(&self, attempt: u32) {
        if attempt == 0 {
            return;
        }
        let ms = self.base_delay.as_millis() as u64 * (1u64 << attempt.min(6));
        sleep(Duration::from_millis(ms.min(2_000))).await;
    }
}
