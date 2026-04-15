//! OOM-kill related CLI defaults and parsing.

use clap::Parser;
use yr_runtime_manager::Config;

#[test]
fn oom_defaults_are_conservative() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert!(!c.oom_kill_enable);
    assert_eq!(c.oom_kill_control_limit, 0);
    assert_eq!(c.oom_consecutive_detection_count, 3);
}

#[test]
fn oom_flags_parse_together() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--oom-kill-enable",
        "--oom-kill-control-limit",
        "2048",
        "--oom-consecutive-detection-count",
        "7",
    ])
    .unwrap();
    assert!(c.oom_kill_enable);
    assert_eq!(c.oom_kill_control_limit, 2048);
    assert_eq!(c.oom_consecutive_detection_count, 7);
}
