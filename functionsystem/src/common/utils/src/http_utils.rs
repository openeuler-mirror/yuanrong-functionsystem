//! HTTP helpers ported from `functionsystem/src/common/http/http_util.{h,cpp}` plus shared `/healthy` probe logic.

use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use std::collections::BTreeMap;

pub const METHOD_GET: &str = "GET";
pub const HEADER_CONNECTION: &str = "connection";
pub const HEADER_AUTHORIZATION: &str = "authorization";

/// SHA-256 of empty body (hex, lowercase).
pub const EMPTY_CONTENT_SHA256: &str =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

fn should_query_escape(c: u8) -> bool {
    let ch = c as char;
    if ch.is_ascii_alphanumeric() {
        return false;
    }
    if matches!(c, b'-' | b'_' | b'.' | b'~') {
        return false;
    }
    true
}

/// Query-string escaping (`EscapeQuery` in C++).
pub fn escape_query(s: &str) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut out = String::new();
    for &c in s.as_bytes() {
        if !should_query_escape(c) {
            out.push(c as char);
        } else if c == b' ' {
            out.push('+');
        } else {
            out.push('%');
            out.push(HEX[(c >> 4) as usize] as char);
            out.push(HEX[(c & 0xf) as usize] as char);
        }
    }
    out
}

fn do_replace(s: &mut String, from: &str, to: &str) {
    while let Some(i) = s.find(from) {
        s.replace_range(i..i + from.len(), to);
    }
}

/// URL escaping (`EscapeURL` in C++).
pub fn escape_url(url: &str, replace_path: bool) -> String {
    if url.is_empty() {
        return String::new();
    }
    let mut encodeurl = escape_query(url);
    do_replace(&mut encodeurl, "+", "%20");
    do_replace(&mut encodeurl, "*", "%2A");
    do_replace(&mut encodeurl, "%7E", "~");
    if replace_path {
        do_replace(&mut encodeurl, "%2F", "/");
    }
    encodeurl
}

fn header_key_lower(key: &str) -> String {
    key.to_ascii_lowercase()
}

fn trim_header_value(value: &str) -> String {
    value.trim().to_string()
}

/// Headers contributing to the canonical string (skips connection / authorization), sorted by key.
pub fn get_canonical_headers(headers: &BTreeMap<String, String>) -> String {
    let mut out = String::new();
    for (k, v) in headers {
        let lower = header_key_lower(k);
        if lower == HEADER_CONNECTION || lower == HEADER_AUTHORIZATION {
            continue;
        }
        out.push_str(&lower);
        out.push(':');
        out.push_str(&trim_header_value(v));
        out.push('\n');
    }
    out
}

pub fn get_signed_headers(headers: &BTreeMap<String, String>) -> String {
    let mut first = true;
    let mut out = String::new();
    for k in headers.keys() {
        let lower = header_key_lower(k);
        if lower == HEADER_CONNECTION || lower == HEADER_AUTHORIZATION {
            continue;
        }
        if first {
            first = false;
            out.push_str(&lower);
        } else {
            out.push(';');
            out.push_str(&lower);
        }
    }
    out
}

pub fn get_canonical_queries(queries: Option<&BTreeMap<String, String>>) -> String {
    let Some(q) = queries else {
        return String::new();
    };
    let mut first = true;
    let mut out = String::new();
    for (k, v) in q {
        if first {
            first = false;
        } else {
            out.push('&');
        }
        out.push_str(&escape_url(k, false));
        out.push('=');
        out.push_str(&escape_url(v, false));
    }
    out
}

pub fn get_canonical_request(
    method: &str,
    path: &str,
    queries: Option<&BTreeMap<String, String>>,
    headers: &BTreeMap<String, String>,
    sha256: &str,
) -> String {
    let canonical_path = if path.is_empty() {
        "/".to_string()
    } else {
        escape_url(path, true)
    };
    let canonical_queries = get_canonical_queries(queries);
    let canonical_headers = get_canonical_headers(headers);
    let signed_headers = get_signed_headers(headers);
    let body_hash = if sha256.is_empty() {
        EMPTY_CONTENT_SHA256
    } else {
        sha256
    };
    format!(
        "{method}\n{canonical_path}\n{canonical_queries}\n{canonical_headers}\n{signed_headers}\n{body_hash}"
    )
}

/// State for [`default_healthy_handler`]: matches `node-id` and process `pid` headers (see yr-agent / yr-proxy).
#[derive(Clone)]
pub struct HealthyProbeState {
    pub node_id: String,
}

/// Axum handler: returns 400 on mismatch, 200 with empty body on success.
pub async fn default_healthy_handler(
    State(st): State<HealthyProbeState>,
    headers: HeaderMap,
) -> Response {
    let expected_node = st.node_id.as_str();
    let node_ok = headers
        .get("node-id")
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .is_some_and(|v| v == expected_node);
    if !node_ok {
        return (StatusCode::BAD_REQUEST, "error nodeID").into_response();
    }
    let pid = std::process::id();
    let pid_ok = headers
        .get("pid")
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    (StatusCode::OK, "").into_response()
}

/// Header-only check without Axum (for tests and non-Axum servers).
pub fn healthy_headers_valid(
    expected_node_id: &str,
    node_id_header: Option<&str>,
    pid_header: Option<&str>,
) -> Result<(), &'static str> {
    let node_ok = node_id_header
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .is_some_and(|v| v == expected_node_id);
    if !node_ok {
        return Err("error nodeID");
    }
    let pid = std::process::id();
    let pid_ok = pid_header
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return Err("error PID");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn escape_query_matches_cpp_fixture() {
        let input = "Hello World-1_2.3~!+@#$%^&*()";
        let expected = "Hello+World-1_2.3~%21%2B%40%23%24%25%5E%26%2A%28%29";
        assert_eq!(escape_query(input), expected);
        assert_eq!(escape_query(""), "");
        assert_eq!(escape_query("123"), "123");
    }

    #[test]
    fn escape_url_fixture() {
        let url = "https://www.example.com/path/to/resource?param=value 1+2*3~4";
        let expected_not_replace =
            "https%3A%2F%2Fwww.example.com%2Fpath%2Fto%2Fresource%3Fparam%3Dvalue%201%2B2%2A3~4";
        assert_eq!(escape_url(url, false), expected_not_replace);
        assert_eq!(
            escape_url(url, true),
            "https%3A//www.example.com/path/to/resource%3Fparam%3Dvalue%201%2B2%2A3~4"
        );
    }

    #[test]
    fn canonical_request_matches_cpp_test() {
        let mut queries = BTreeMap::new();
        queries.insert("p2".into(), "value2".into());
        queries.insert("p3".into(), "value3".into());
        queries.insert("p1".into(), "value1".into());
        let mut headers = BTreeMap::new();
        headers.insert("h2".into(), "**".into());
        headers.insert("Host".into(), "example.com".into());
        let got = get_canonical_request(
            METHOD_GET,
            "/path/to/resource",
            Some(&queries),
            &headers,
            EMPTY_CONTENT_SHA256,
        );
        let expected = "GET\n/path/to/resource\np1=value1&p2=value2&p3=value3\nhost:example.com\nh2:**\n\nhost;h2\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        assert_eq!(got, expected);
    }

    #[test]
    fn canonical_request_skips_auth_headers() {
        let mut headers = BTreeMap::new();
        headers.insert(HEADER_AUTHORIZATION.to_string(), "**".into());
        headers.insert(HEADER_CONNECTION.to_string(), "**".into());
        let got = get_canonical_request(METHOD_GET, "", None, &headers, "");
        let expected = "GET\n/\n\n\n\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        assert_eq!(got, expected);
    }
}
