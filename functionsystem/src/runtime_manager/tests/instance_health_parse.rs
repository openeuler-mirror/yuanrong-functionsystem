//! `config_json.health` parsing (startup grace, HTTP/TCP probes).

use std::time::Duration;

use yr_runtime_manager::instance_health::parse_from_config_json;

#[test]
fn empty_config_uses_default_startup_deadline() {
    let d = Duration::from_secs(99);
    let spec = parse_from_config_json("", d);
    assert!(spec.http_url.is_none());
    assert!(spec.tcp_host.is_none());
    assert!(spec.tcp_port.is_none());
    assert_eq!(spec.startup_deadline, d);
}

#[test]
fn health_block_parses_http_tcp_and_startup_seconds() {
    let json = r#"{"health":{"http":"http://127.0.0.1:8080/ready","tcp":"10.0.0.2:7777","startup_seconds":12}}"#;
    let spec = parse_from_config_json(json, Duration::from_secs(30));
    assert_eq!(spec.http_url.as_deref(), Some("http://127.0.0.1:8080/ready"));
    assert_eq!(spec.tcp_host.as_deref(), Some("10.0.0.2"));
    assert_eq!(spec.tcp_port, Some(7777));
    assert_eq!(spec.startup_deadline, Duration::from_secs(12));
}

#[test]
fn startup_seconds_clamped_to_at_least_one_second() {
    let json = r#"{"health":{"startup_seconds":0}}"#;
    let spec = parse_from_config_json(json, Duration::from_secs(5));
    assert_eq!(spec.startup_deadline, Duration::from_secs(1));
}
