//! Traefik dynamic-route registration for sandbox port forwards — port of the C++
//! `local_scheduler/traefik_registry/traefik_registry.cpp`.
//!
//! When an instance with forwarded ports reaches RUNNING, the proxy writes traefik
//! KV-provider keys into etcd so external traffic on the traefik entrypoint routes
//! `/{instanceID}/{containerPort}` to `http(s)://{proxyIP}:{hostPort}`. The key/value
//! layout below matches the C++ implementation byte-for-byte.

use std::sync::Arc;

use tokio::sync::Mutex;
use tracing::{info, warn};
use yr_metastore_client::MetaStoreClient;

/// One allocated forward (C++ `TraefikRegistry::PortMapping`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PortMapping {
    pub sandbox_port: u32,
    pub host_port: u32,
    /// "https" (case-insensitive) => HTTPS backend with serversTransport; anything
    /// else => HTTP backend.
    pub protocol: String,
}

/// Parse the runtime manager's port mappings JSON: `["protocol:host:container", ...]`
/// (3-part) or the legacy `["host:container", ...]` (2-part, protocol defaults to
/// "http"). Malformed entries are skipped; bad JSON yields an empty list.
pub fn parse_port_mappings(json: &str) -> Vec<PortMapping> {
    let Ok(entries) = serde_json::from_str::<Vec<String>>(json) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for entry in entries {
        let parts: Vec<&str> = entry.split(':').collect();
        match parts.as_slice() {
            [protocol, host, container] => {
                if let (Ok(h), Ok(c)) = (host.parse::<u32>(), container.parse::<u32>()) {
                    out.push(PortMapping {
                        sandbox_port: c,
                        host_port: h,
                        protocol: (*protocol).to_string(),
                    });
                }
            }
            [host, container] => {
                if let (Ok(h), Ok(c)) = (host.parse::<u32>(), container.parse::<u32>()) {
                    out.push(PortMapping {
                        sandbox_port: c,
                        host_port: h,
                        protocol: "http".to_string(),
                    });
                }
            }
            _ => {}
        }
    }
    out
}

/// Traefik forbids `@` in router/service names (C++ `SanitizeID`).
fn sanitize_id(id: &str) -> String {
    id.replace('@', "-at-")
}

/// Build the etcd key-value pairs for one instance registration. Pure (unit-tested
/// against the C++ key layout). Includes the global StripPrefix middleware key
/// (idempotent; C++ writes it once in the constructor).
pub fn build_register_kvs(
    key_prefix: &str,
    http_entry_point: &str,
    enable_tls: bool,
    servers_transport: &str,
    instance_id: &str,
    host_ip: &str,
    mappings: &[PortMapping],
) -> Vec<(String, String)> {
    let safe_id = sanitize_id(instance_id);
    let mut kvs = vec![(
        format!("{key_prefix}/http/middlewares/stripprefix-all/stripPrefixRegex/regex"),
        "^/[^/]+/[0-9]+".to_string(),
    )];
    for m in mappings {
        let router = format!("{safe_id}-p{}", m.sandbox_port);
        let prefix_path = format!("/{safe_id}/{}", m.sandbox_port);
        let https_backend = m.protocol.eq_ignore_ascii_case("https");
        let scheme = if https_backend { "https" } else { "http" };

        kvs.push((
            format!("{key_prefix}/http/routers/{router}/rule"),
            format!("PathPrefix(`{prefix_path}`)"),
        ));
        kvs.push((
            format!("{key_prefix}/http/routers/{router}/service"),
            router.clone(),
        ));
        kvs.push((
            format!("{key_prefix}/http/routers/{router}/middlewares/0"),
            "stripprefix-all".to_string(),
        ));
        kvs.push((
            format!("{key_prefix}/http/routers/{router}/entryPoints/0"),
            http_entry_point.to_string(),
        ));
        if enable_tls {
            kvs.push((format!("{key_prefix}/http/routers/{router}/tls"), String::new()));
        }
        kvs.push((
            format!("{key_prefix}/http/services/{router}/loadbalancer/servers/0/url"),
            format!("{scheme}://{host_ip}:{}", m.host_port),
        ));
        if https_backend && !servers_transport.is_empty() {
            kvs.push((
                format!("{key_prefix}/http/services/{router}/loadbalancer/serverstransport"),
                servers_transport.to_string(),
            ));
        }
    }
    kvs
}

pub struct TraefikRegistry {
    etcd: Arc<Mutex<MetaStoreClient>>,
    key_prefix: String,
    http_entry_point: String,
    enable_tls: bool,
    servers_transport: String,
    host_ip: String,
}

impl TraefikRegistry {
    pub fn new(
        etcd: Arc<Mutex<MetaStoreClient>>,
        key_prefix: &str,
        http_entry_point: &str,
        enable_tls: bool,
        servers_transport: &str,
        host_ip: &str,
    ) -> Self {
        // C++ defaults: prefix "traefik", entrypoint "websecure", transport
        // "yr-backend-tls@file".
        let key_prefix = if key_prefix.trim().is_empty() {
            "traefik"
        } else {
            key_prefix.trim()
        };
        let http_entry_point = if http_entry_point.trim().is_empty() {
            "websecure"
        } else {
            http_entry_point.trim()
        };
        Self {
            etcd,
            key_prefix: key_prefix.to_string(),
            http_entry_point: http_entry_point.to_string(),
            enable_tls,
            servers_transport: servers_transport.trim().to_string(),
            host_ip: host_ip.to_string(),
        }
    }

    /// Register the instance's forwarded ports (no-op when the mappings JSON has
    /// none — process-mode instances).
    pub async fn register_instance(&self, instance_id: &str, port_mappings_json: &str) {
        let mappings = parse_port_mappings(port_mappings_json);
        if mappings.is_empty() {
            return;
        }
        let kvs = build_register_kvs(
            &self.key_prefix,
            &self.http_entry_point,
            self.enable_tls,
            &self.servers_transport,
            instance_id,
            &self.host_ip,
            &mappings,
        );
        let mut etcd = self.etcd.lock().await;
        let mut failed = 0usize;
        for (k, v) in &kvs {
            if let Err(e) = etcd.put(k, v.as_bytes()).await {
                warn!(key = %k, error = %e, "traefik register put failed");
                failed += 1;
            }
        }
        if failed == 0 {
            info!(%instance_id, ports = mappings.len(), "registered instance to traefik");
        }
    }

    /// Remove all routers/services for the instance (prefix delete — router names
    /// start with the sanitized instance id, mirroring the C++ prefix delete).
    pub async fn unregister_instance(&self, instance_id: &str) {
        let safe_id = sanitize_id(instance_id);
        let mut etcd = self.etcd.lock().await;
        for prefix in [
            format!("{}/http/routers/{safe_id}", self.key_prefix),
            format!("{}/http/services/{safe_id}", self.key_prefix),
        ] {
            if let Err(e) = etcd.delete_prefix(&prefix).await {
                warn!(prefix = %prefix, error = %e, "traefik unregister delete failed");
            }
        }
        info!(%instance_id, "unregistered instance from traefik");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_three_part_and_legacy_two_part_mappings() {
        let v = parse_port_mappings(r#"["tcp:21006:8080","https:40002:443","40003:9090","bad","x:y:z"]"#);
        assert_eq!(
            v,
            vec![
                PortMapping { sandbox_port: 8080, host_port: 21006, protocol: "tcp".into() },
                PortMapping { sandbox_port: 443, host_port: 40002, protocol: "https".into() },
                PortMapping { sandbox_port: 9090, host_port: 40003, protocol: "http".into() },
            ]
        );
        assert!(parse_port_mappings("not json").is_empty());
        assert!(parse_port_mappings("").is_empty());
    }

    #[test]
    fn builds_cpp_compatible_keys_http_no_tls() {
        let kvs = build_register_kvs(
            "traefik",
            "web",
            false,
            "yr-backend-tls@file",
            "default-sbx",
            "172.18.0.2",
            &[PortMapping { sandbox_port: 8080, host_port: 21006, protocol: "tcp".into() }],
        );
        let map: std::collections::HashMap<_, _> = kvs.into_iter().collect();
        assert_eq!(
            map["traefik/http/middlewares/stripprefix-all/stripPrefixRegex/regex"],
            "^/[^/]+/[0-9]+"
        );
        assert_eq!(
            map["traefik/http/routers/default-sbx-p8080/rule"],
            "PathPrefix(`/default-sbx/8080`)"
        );
        assert_eq!(map["traefik/http/routers/default-sbx-p8080/service"], "default-sbx-p8080");
        assert_eq!(map["traefik/http/routers/default-sbx-p8080/middlewares/0"], "stripprefix-all");
        assert_eq!(map["traefik/http/routers/default-sbx-p8080/entryPoints/0"], "web");
        assert!(!map.contains_key("traefik/http/routers/default-sbx-p8080/tls"));
        assert_eq!(
            map["traefik/http/services/default-sbx-p8080/loadbalancer/servers/0/url"],
            "http://172.18.0.2:21006"
        );
        // tcp protocol is an HTTP backend: no serversTransport key
        assert!(!map
            .contains_key("traefik/http/services/default-sbx-p8080/loadbalancer/serverstransport"));
    }

    #[test]
    fn https_protocol_gets_tls_router_and_servers_transport() {
        let kvs = build_register_kvs(
            "traefik",
            "websecure",
            true,
            "yr-backend-tls@file",
            "t@inst",
            "10.0.0.1",
            &[PortMapping { sandbox_port: 443, host_port: 40002, protocol: "HTTPS".into() }],
        );
        let map: std::collections::HashMap<_, _> = kvs.into_iter().collect();
        // '@' sanitized to -at-
        assert_eq!(
            map["traefik/http/routers/t-at-inst-p443/rule"],
            "PathPrefix(`/t-at-inst/443`)"
        );
        assert_eq!(map["traefik/http/routers/t-at-inst-p443/tls"], "");
        assert_eq!(
            map["traefik/http/services/t-at-inst-p443/loadbalancer/servers/0/url"],
            "https://10.0.0.1:40002"
        );
        assert_eq!(
            map["traefik/http/services/t-at-inst-p443/loadbalancer/serverstransport"],
            "yr-backend-tls@file"
        );
    }
}
