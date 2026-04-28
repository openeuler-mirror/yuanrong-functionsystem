//! C++ busproxy invoke memory admission compatibility tests.

use yr_proxy::invoke_admission::{InvokeMemoryConfig, InvokeMemoryMonitor};

#[test]
fn invoke_memory_monitor_rejects_when_high_threshold_would_be_exceeded() {
    let monitor = InvokeMemoryMonitor::new(InvokeMemoryConfig {
        enable: true,
        low_memory_threshold: 0.6,
        high_memory_threshold: 0.8,
        message_size_threshold: 20,
    });

    assert!(!monitor.allow_with_usage("inst-a", "req-a", 101, 1000, 700));
}

#[test]
fn invoke_memory_monitor_allows_small_messages_below_size_threshold() {
    let monitor = InvokeMemoryMonitor::new(InvokeMemoryConfig {
        enable: true,
        low_memory_threshold: 0.6,
        high_memory_threshold: 0.8,
        message_size_threshold: 20,
    });

    assert!(monitor.allow_with_usage("inst-a", "req-a", 20, 1000, 700));
}

#[test]
fn invoke_memory_monitor_applies_low_threshold_fair_sharing_and_release() {
    let monitor = InvokeMemoryMonitor::new(InvokeMemoryConfig {
        enable: true,
        low_memory_threshold: 0.6,
        high_memory_threshold: 0.8,
        message_size_threshold: 20,
    });

    assert!(monitor.allow_with_usage("inst-a", "req-a", 100, 1000, 500));
    assert_eq!(monitor.estimated_usage(), 100);

    assert!(
        !monitor.allow_with_usage("inst-a", "req-b", 100, 1000, 650),
        "same instance over average estimate is rejected once low threshold is active"
    );

    monitor.release("inst-a", "req-a");
    assert_eq!(monitor.estimated_usage(), 0);
    assert!(monitor.allow_with_usage("inst-a", "req-c", 100, 1000, 650));
}
