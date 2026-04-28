//! C++ create token-bucket compatibility tests.

use yr_proxy::local_scheduler::CreateRateLimiter;

#[tokio::test]
async fn create_rate_limiter_is_per_tenant_and_skips_system_tenant() {
    let limiter = CreateRateLimiter::new(1).expect("enabled");

    assert!(limiter.acquire_for_tenant("tenant-a").await);
    assert!(!limiter.acquire_for_tenant("tenant-a").await);
    assert!(limiter.acquire_for_tenant("tenant-b").await);

    for _ in 0..3 {
        assert!(limiter.acquire_for_tenant("0").await);
    }
}
