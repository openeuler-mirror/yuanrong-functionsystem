//! Runtime child-exit classification aligned with C++ HealthCheckActor fallback semantics.

use nix::sys::signal::Signal;
use nix::sys::wait::WaitStatus;
use nix::unistd::Pid;
use yr_runtime_manager::health_check::classify_wait_status;

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
