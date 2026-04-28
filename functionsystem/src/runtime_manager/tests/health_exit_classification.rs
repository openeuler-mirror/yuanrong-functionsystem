//! Runtime child-exit classification aligned with C++ HealthCheckActor fallback semantics.

use nix::sys::signal::Signal;
use nix::sys::wait::WaitStatus;
use nix::unistd::Pid;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use yr_runtime_manager::health_check::{
    classify_wait_status, dmesg_oom_info, runtime_exit_log_message,
    runtime_exit_log_message_with_dmesg,
};

#[test]
fn zero_exit_is_runtime_returned() {
    let ev = classify_wait_status(WaitStatus::Exited(Pid::from_raw(101), 0)).unwrap();
    assert_eq!(ev.pid, 101);
    assert_eq!(ev.status, "returned");
    assert_eq!(ev.exit_code, 0);
    assert_eq!(ev.error_message, "runtime had been returned");
}

#[test]
fn nonzero_exit_is_failed_unknown_error() {
    let ev = classify_wait_status(WaitStatus::Exited(Pid::from_raw(102), 7)).unwrap();
    assert_eq!(ev.pid, 102);
    assert_eq!(ev.status, "failed");
    assert_eq!(ev.exit_code, 7);
    assert!(ev
        .error_message
        .contains("an unknown error caused the instance exited. exit code:7"));
}

#[test]
fn signal_exit_is_failed_with_signal_context() {
    let ev = classify_wait_status(WaitStatus::Signaled(
        Pid::from_raw(103),
        Signal::SIGTERM,
        false,
    ))
    .unwrap();
    assert_eq!(ev.pid, 103);
    assert_eq!(ev.status, "failed");
    assert_eq!(ev.exit_code, -15);
    assert!(ev.error_message.contains("terminated by signal SIGTERM"));
}

#[test]
fn still_alive_is_not_an_exit_event() {
    assert!(classify_wait_status(WaitStatus::StillAlive).is_none());
}

#[test]
fn exception_backtrace_log_overrides_unknown_exit_message() {
    let root = unique_temp_dir("yr-rm-backtrace");
    let exception_dir = root.join("exception");
    fs::create_dir_all(&exception_dir).unwrap();
    fs::write(
        exception_dir.join("BackTrace_runtime-1.log"),
        "panic stack from runtime\nframe-1\n",
    )
    .unwrap();

    let msg = runtime_exit_log_message(
        "runtime-1",
        "instance-1",
        7,
        &root,
        "",
        "node-a",
        &root.join("raw"),
    )
    .unwrap();

    assert_eq!(msg, "panic stack from runtime\nframe-1\n");
}

#[test]
fn std_error_log_is_used_when_backtrace_is_absent() {
    let root = unique_temp_dir("yr-rm-std-log");
    let std_dir = root.join("std");
    fs::create_dir_all(&std_dir).unwrap();
    fs::write(
        std_dir.join("node-a-user_func_std.log"),
        [
            "2026|instance-1|runtime-1|INFO|not an error",
            "2026|instance-2|runtime-2|ERROR|wrong runtime",
            "2026|instance-1|runtime-1|ERROR|first useful error",
            "2026|instance-1|runtime-1|ERROR|second useful error",
        ]
        .join("\n"),
    )
    .unwrap();

    let msg = runtime_exit_log_message_with_dmesg(
        "runtime-1",
        "instance-1",
        9,
        &root,
        "std",
        "node-a",
        &root,
        None,
        true,
    )
    .unwrap();

    assert!(msg.contains("instance(instance-1) runtime(runtime-1) exit code(9)"));
    assert!(msg.contains("first useful error"));
    assert!(msg.contains("second useful error"));
    assert!(!msg.contains("not an error"));
    assert!(!msg.contains("wrong runtime"));
}

#[test]
fn dmesg_oom_message_is_used_before_std_error_log() {
    let root = unique_temp_dir("yr-rm-dmesg-oom");
    let std_dir = root.join("std");
    fs::create_dir_all(&std_dir).unwrap();
    fs::write(
        std_dir.join("node-a-user_func_std.log"),
        "2026|instance-1|runtime-1|ERROR|std log should lose to oom\n",
    )
    .unwrap();

    let msg = runtime_exit_log_message_with_dmesg(
        "runtime-1",
        "instance-1",
        9,
        &root,
        "std",
        "node-a",
        &root,
        Some("Memory cgroup out of memory: Killed process 123(runtime)"),
        true,
    )
    .unwrap();

    assert_eq!(
        msg,
        "runtime(runtime-1) process may be killed for some reason"
    );
}

#[test]
fn container_dmesg_oom_requires_kubepods_limit_marker() {
    assert!(dmesg_oom_info("Killed process 123(runtime)", false).is_none());

    let msg = dmesg_oom_info(
        "noise before killed as a result of limit of /kubepods xxx\nKilled process 123(runtime)",
        false,
    )
    .unwrap();

    assert!(msg.contains("Killed process 123"));
}

#[test]
fn rust_captured_stderr_is_used_when_cpp_std_log_is_absent() {
    let root = unique_temp_dir("yr-rm-raw-stderr");
    fs::write(
        root.join("runtime-1.stderr.log"),
        "Traceback from python runtime\npanic without cxx prefix\n",
    )
    .unwrap();

    let msg = runtime_exit_log_message_with_dmesg(
        "runtime-1",
        "instance-1",
        11,
        &root,
        "std",
        "node-a",
        &root,
        None,
        true,
    )
    .unwrap();

    assert!(msg.contains("instance(instance-1) runtime(runtime-1) exit code(11)"));
    assert!(msg.contains("Traceback from python runtime"));
    assert!(msg.contains("panic without cxx prefix"));
}

fn unique_temp_dir(prefix: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir = std::env::temp_dir().join(format!("{prefix}-{}-{nanos}", std::process::id()));
    if Path::new(&dir).exists() {
        fs::remove_dir_all(&dir).unwrap();
    }
    fs::create_dir_all(&dir).unwrap();
    dir
}
