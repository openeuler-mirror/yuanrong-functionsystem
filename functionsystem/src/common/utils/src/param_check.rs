//! Port of `functionsystem/src/common/utils/param_check.h`.

use regex::Regex;
use std::collections::HashSet;
use std::sync::OnceLock;

pub const NODE_ID_CHECK_PATTERN: &str = r"^[^/\s]{1,128}$";
pub const ALIAS_CHECK_PATTERN: &str = r"^[^/\s]{0,128}$";

pub const IP_CHECK_PATTERN: &str =
    r"^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$";

pub const ADDRESSES_CHECK_PATTERN: &str = r"^(((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?):([0-9]{1,5})(,|$))+$";

pub const INNER_SERVICE_ADDRESS_SUFFIX: &str = "svc.cluster.local";

pub const MIN_PORT: i32 = 0;
pub const MAX_PORT: i32 = 65535;

macro_rules! cached_regex {
    ($name:ident, $pat:expr) => {
        fn $name() -> &'static Regex {
            static R: OnceLock<Regex> = OnceLock::new();
            R.get_or_init(|| Regex::new($pat).expect("valid regex"))
        }
    };
}

cached_regex!(re_node, NODE_ID_CHECK_PATTERN);
cached_regex!(re_alias, ALIAS_CHECK_PATTERN);
cached_regex!(re_ip, IP_CHECK_PATTERN);
cached_regex!(re_addrs, ADDRESSES_CHECK_PATTERN);

pub fn is_node_id_valid(node_id: &str) -> bool {
    re_node().is_match(node_id)
}

pub fn is_alias_valid(alias: &str) -> bool {
    re_alias().is_match(alias)
}

pub fn is_ip_valid(ip: &str) -> bool {
    re_ip().is_match(ip)
}

pub fn is_inner_service_address(ip: &str) -> bool {
    ip.ends_with(INNER_SERVICE_ADDRESS_SUFFIX)
}

pub fn is_addresses_valid(address: &str) -> bool {
    re_addrs().is_match(address)
}

pub fn is_port_valid(port_str: &str) -> bool {
    if port_str.is_empty() {
        return false;
    }
    let Ok(port) = port_str.parse::<i32>() else {
        return false;
    };
    (MIN_PORT..=MAX_PORT).contains(&port)
}

pub fn is_address_valid(address: &str) -> bool {
    let Some(pos) = address.rfind(':') else {
        return false;
    };
    let ip = &address[..pos];
    let port = &address[pos + 1..];
    is_ip_valid(ip) && is_port_valid(port)
}

/// Wraps a value-only check for CLI-style `(flag, value)` callbacks.
pub fn flag_check_wrapper(check: fn(&str) -> bool) -> impl Fn(&str, &mut String) -> bool {
    move |flag_name: &str, input: &mut String| {
        let valid = check(input.as_str());
        if !valid {
            eprintln!("flag: {flag_name} value: {input} is invalid");
        }
        valid
    }
}

pub fn white_list_check(white_list: HashSet<String>) -> impl Fn(&str, &mut String) -> bool {
    move |flag_name: &str, input: &mut String| {
        let valid = white_list.contains(input.as_str());
        if !valid {
            eprintln!("flag: {flag_name} value: {input} is invalid");
        }
        valid
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn node_id_rules() {
        assert!(is_node_id_valid("node-a1"));
        assert!(!is_node_id_valid("a/b"));
        assert!(!is_node_id_valid(""));
    }

    #[test]
    fn ip_and_address() {
        assert!(is_ip_valid("192.168.0.1"));
        assert!(!is_ip_valid("999.0.0.1"));
        assert!(is_address_valid("10.0.0.1:8080"));
        assert!(!is_address_valid("10.0.0.1"));
    }

    #[test]
    fn inner_svc_suffix() {
        assert!(is_inner_service_address("foo.bar.svc.cluster.local"));
        assert!(!is_inner_service_address("localhost"));
    }

    #[test]
    fn white_list_cb() {
        let mut wl = HashSet::new();
        wl.insert("ok".into());
        let f = white_list_check(wl);
        let mut v = "ok".to_string();
        assert!(f("x", &mut v));
        v = "no".to_string();
        assert!(!f("x", &mut v));
    }
}
