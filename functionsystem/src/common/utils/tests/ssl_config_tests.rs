use std::fs;

use yr_common::ssl_config::{get_ssl_cert_config, litebus_ssl_envs, SslInputs};

fn temp_cert_dir(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "yr-ssl-{name}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn ssl_cert_config_disabled_when_ssl_and_metrics_are_off() {
    let dir = temp_cert_dir("disabled");
    let cfg = get_ssl_cert_config(&SslInputs {
        ssl_enable: false,
        metrics_ssl_enable: false,
        ssl_base_path: dir.to_string_lossy().into_owned(),
        ssl_root_file: "ca.pem".into(),
        ssl_cert_file: "cert.pem".into(),
        ssl_key_file: "key.pem".into(),
    });

    assert!(!cfg.is_enable);
    assert!(!cfg.is_metrics_ssl_enable);
    assert!(cfg.cert_path.is_empty());
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn ssl_cert_config_resolves_existing_files_and_maps_litebus_envs() {
    let dir = temp_cert_dir("valid");
    fs::write(dir.join("ca.pem"), "ca").unwrap();
    fs::write(dir.join("cert.pem"), "cert").unwrap();
    fs::write(dir.join("key.pem"), "key").unwrap();

    let cfg = get_ssl_cert_config(&SslInputs {
        ssl_enable: true,
        metrics_ssl_enable: false,
        ssl_base_path: dir.to_string_lossy().into_owned(),
        ssl_root_file: "ca.pem".into(),
        ssl_cert_file: "cert.pem".into(),
        ssl_key_file: "key.pem".into(),
    });

    assert!(cfg.is_enable);
    assert_eq!(
        cfg.cert_path,
        fs::canonicalize(&dir).unwrap().to_string_lossy()
    );
    assert!(cfg.root_cert_file.ends_with("ca.pem"));
    let envs = litebus_ssl_envs(&cfg).expect("ssl envs");
    assert_eq!(
        envs.get("LITEBUS_SSL_ENABLED").map(String::as_str),
        Some("1")
    );
    assert_eq!(
        envs.get("LITEBUS_SSL_VERIFY_CERT").map(String::as_str),
        Some("1")
    );
    assert_eq!(
        envs.get("LITEBUS_SSL_DECRYPT_TYPE").map(String::as_str),
        Some("0")
    );
    assert_eq!(envs.get("LITEBUS_SSL_CA_FILE"), Some(&cfg.root_cert_file));
    assert_eq!(envs.get("LITEBUS_SSL_CA_DIR"), Some(&cfg.cert_path));
    assert_eq!(envs.get("LITEBUS_SSL_CERT_FILE"), Some(&cfg.cert_file));
    assert_eq!(envs.get("LITEBUS_SSL_KEY_FILE"), Some(&cfg.key_file));
    let _ = fs::remove_dir_all(dir);
}

#[test]
fn ssl_cert_config_missing_any_file_stays_disabled_like_cpp() {
    let dir = temp_cert_dir("missing");
    fs::write(dir.join("ca.pem"), "ca").unwrap();
    fs::write(dir.join("cert.pem"), "cert").unwrap();

    let cfg = get_ssl_cert_config(&SslInputs {
        ssl_enable: true,
        metrics_ssl_enable: false,
        ssl_base_path: dir.to_string_lossy().into_owned(),
        ssl_root_file: "ca.pem".into(),
        ssl_cert_file: "cert.pem".into(),
        ssl_key_file: "key.pem".into(),
    });

    assert!(!cfg.is_enable);
    assert!(litebus_ssl_envs(&cfg).is_err());
    let _ = fs::remove_dir_all(dir);
}
