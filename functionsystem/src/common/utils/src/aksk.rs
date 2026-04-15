//! AK/SK content and request signing (ports of `aksk_content.h`, `sign_request.h`, `aksk_util.cpp` core logic).

use crate::http_utils::{self, get_canonical_request};
use crate::status::{Status, StatusCode};
use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::time::{SystemTime, UNIX_EPOCH};

type HmacSha256 = Hmac<Sha256>;

pub const HEADER_TOKEN_KEY: &str = "X-Signature";
pub const HEADER_SIGNED_HEADER_KEY: &str = "X-Signed-Header";

pub const TENANT_ID_STR: &str = "tenantID";
pub const ACCESS_KEY_STR: &str = "accessKey";
pub const SECRET_KEY_STR: &str = "secretKey";
pub const DATA_KEY_STR: &str = "dataKey";
pub const EXPIRED_TIME_STAMP_STR: &str = "expiredTimeStamp";
pub const EXPIRED_TIME_SPAN_STR: &str = "expiredTimeSpan";
pub const CREDENTIAL_NAME_KEY_STR: &str = "credentialName";
pub const SERVICE_NAME_KEY_STR: &str = "serviceName";
pub const MICROSERVICE_NAMES_KEY_STR: &str = "microserviceNames";
pub const ROLE_STR: &str = "role";
pub const SYSTEM_ROLE: &str = "system";
pub const NORMAL_ROLE: &str = "normal";
pub const UNCONFIRMED_ROLE: &str = "unconfirmed";
pub const NEW_CREDENTIAL_EXPIRED_OFFSET: u32 = 30;

#[derive(Debug, Clone)]
pub struct AKSKContent {
    pub tenant_id: String,
    pub access_key: String,
    pub secret_key: Vec<u8>,
    pub data_key: Vec<u8>,
    pub expired_time_stamp: u64,
    pub role: String,
    pub status: Status,
}

impl Default for AKSKContent {
    fn default() -> Self {
        Self {
            tenant_id: String::new(),
            access_key: String::new(),
            secret_key: Vec::new(),
            data_key: Vec::new(),
            expired_time_stamp: 0,
            role: UNCONFIRMED_ROLE.to_string(),
            status: Status::ok(),
        }
    }
}

impl AKSKContent {
    pub fn is_valid(&self, offset_seconds: u32) -> Status {
        if self.status.is_error() {
            return self.status.clone();
        }
        if self.tenant_id.is_empty() {
            return Status::new(StatusCode::ParameterError, "aksk tenantID is empty");
        }
        if self.access_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "aksk accessKey is empty");
        }
        if self.secret_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "aksk secretKey is empty");
        }
        if self.data_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "aksk dataKey is empty");
        }
        let now = unix_now_secs();
        if self.expired_time_stamp < now + u64::from(offset_seconds) && self.expired_time_stamp > 0 {
            return Status::new(
                StatusCode::ParameterError,
                format!(
                    "aksk expired time stamp is earlier than now, expiredTimeStamp: {}",
                    self.expired_time_stamp
                ),
            );
        }
        Status::ok()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EncAKSKContent {
    #[serde(default, rename = "tenantID")]
    pub tenant_id: String,
    #[serde(default, rename = "accessKey")]
    pub access_key: String,
    #[serde(default, rename = "secretKey")]
    pub secret_key: String,
    #[serde(default, rename = "dataKey")]
    pub data_key: String,
    #[serde(default, rename = "expiredTimeStamp")]
    pub expired_time_stamp: u64,
    #[serde(default, rename = "expiredTimeSpan")]
    pub expired_time_span: u64,
    #[serde(default, rename = "role")]
    pub role: String,
    #[serde(skip, default = "Status::ok")]
    pub status: Status,
}

impl Default for EncAKSKContent {
    fn default() -> Self {
        Self {
            tenant_id: String::new(),
            access_key: String::new(),
            secret_key: String::new(),
            data_key: String::new(),
            expired_time_stamp: 0,
            expired_time_span: 0,
            role: UNCONFIRMED_ROLE.to_string(),
            status: Status::ok(),
        }
    }
}

impl EncAKSKContent {
    pub fn is_valid(&self, offset_seconds: u32) -> Status {
        if self.status.is_error() {
            return self.status.clone();
        }
        if self.tenant_id.is_empty() {
            return Status::new(StatusCode::ParameterError, "encrypt aksk tenantID is empty");
        }
        if self.access_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "encrypt aksk accessKey is empty");
        }
        if self.secret_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "encrypt aksk secretKey is empty");
        }
        if self.data_key.is_empty() {
            return Status::new(StatusCode::ParameterError, "encrypt aksk dataKey is empty");
        }
        let now = unix_now_secs();
        if self.expired_time_stamp < now + u64::from(offset_seconds) && self.expired_time_stamp > 0 {
            return Status::new(
                StatusCode::ParameterError,
                format!(
                    "encrypt aksk expired time stamp is earlier than now, expiredTimeStamp: {}",
                    self.expired_time_stamp
                ),
            );
        }
        Status::ok()
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct PermanentCredential {
    #[serde(default, rename = "tenantID")]
    pub tenant_id: String,
    #[serde(default, rename = "credentialName")]
    pub credential_name: String,
    #[serde(default, rename = "serviceName")]
    pub service_name: String,
    #[serde(default, rename = "microserviceNames")]
    pub microservice_names: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct SignRequest {
    pub method: String,
    pub path: String,
    pub queries: Option<BTreeMap<String, String>>,
    pub headers: BTreeMap<String, String>,
    pub body: String,
}

impl SignRequest {
    pub fn new(
        method: impl Into<String>,
        path: impl Into<String>,
        queries: Option<BTreeMap<String, String>>,
        headers: BTreeMap<String, String>,
        body: impl Into<String>,
    ) -> Self {
        Self {
            method: method.into(),
            path: path.into(),
            queries,
            headers,
            body: body.into(),
        }
    }
}

pub struct AkskKey<'a> {
    pub access_key_id: &'a str,
    pub secret_key: &'a [u8],
}

fn unix_now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

pub fn sha256_hex_lower(data: &[u8]) -> String {
    let digest = Sha256::digest(data);
    hex::encode(digest)
}

pub fn gen_signature_data(canonical_request: &str, timestamp: &str) -> String {
    let mut out = String::with_capacity(timestamp.len() + 1 + 64);
    out.push_str(timestamp);
    out.push(' ');
    out.push_str(&sha256_hex_lower(canonical_request.as_bytes()));
    out
}

fn hmac_sha256_hex(secret: &[u8], data: &str) -> String {
    let mut mac = HmacSha256::new_from_slice(secret).expect("HMAC key length");
    mac.update(data.as_bytes());
    hex::encode(mac.finalize().into_bytes())
}

/// Returns auth headers (`X-Signature`, `X-Signed-Header`).
pub fn sign_http_request(request: &SignRequest, key: &AkskKey<'_>) -> BTreeMap<String, String> {
    let timestamp = chrono::Utc::now().format("%Y%m%dT%H%M%SZ").to_string();
    let body_hash = sha256_hex_lower(request.body.as_bytes());
    let canonical = get_canonical_request(
        &request.method,
        &request.path,
        request.queries.as_ref(),
        &request.headers,
        &body_hash,
    );
    let sig_data = gen_signature_data(&canonical, &timestamp);
    let signature = hmac_sha256_hex(key.secret_key, &sig_data);
    let token = format!(
        "HmacSha256 timestamp={timestamp},ak={},signature={signature}",
        key.access_key_id
    );
    let signed = http_utils::get_signed_headers(&request.headers);
    let mut m = BTreeMap::new();
    m.insert(HEADER_SIGNED_HEADER_KEY.to_string(), signed);
    m.insert(HEADER_TOKEN_KEY.to_string(), token);
    m
}

pub fn verify_http_request(request: &SignRequest, key: &AkskKey<'_>) -> bool {
    let token = match request.headers.get(HEADER_TOKEN_KEY) {
        Some(t) => t,
        None => return false,
    };
    let signed_keys = match request.headers.get(HEADER_SIGNED_HEADER_KEY) {
        Some(s) => s,
        None => return false,
    };
    let (alg, timestamp, ak, expect_sig) = match parse_auth_token(token) {
        Some(x) => x,
        None => return false,
    };
    if alg != "HmacSha256" {
        return false;
    }
    if ak != key.access_key_id {
        return false;
    }
    let signed_set: std::collections::HashSet<String> =
        signed_keys.split(';').map(|s| s.to_ascii_lowercase()).collect();

    let mut filtered = BTreeMap::new();
    for (k, v) in &request.headers {
        let lower = k.to_ascii_lowercase();
        if signed_set.contains(&lower) {
            filtered.insert(k.clone(), v.clone());
        }
    }

    let body_hash = sha256_hex_lower(request.body.as_bytes());
    let canonical = get_canonical_request(
        &request.method,
        &request.path,
        request.queries.as_ref(),
        &filtered,
        &body_hash,
    );
    let actual_sig_data = gen_signature_data(&canonical, &timestamp);
    let actual_sig = hmac_sha256_hex(key.secret_key, &actual_sig_data);
    actual_sig == expect_sig
}

pub fn parse_auth_token(input: &str) -> Option<(String, String, String, String)> {
    let input = input.trim();
    let (alg, rest) = input.split_once(char::is_whitespace)?;
    let mut timestamp = String::new();
    let mut ak = String::new();
    let mut signature = String::new();
    for segment in rest.split(',') {
        let (k, v) = segment.split_once('=')?;
        match k.trim() {
            "timestamp" => timestamp = v.trim().to_string(),
            "ak" => ak = v.trim().to_string(),
            "signature" => signature = v.trim().to_string(),
            _ => {}
        }
    }
    if timestamp.is_empty() || ak.is_empty() || signature.is_empty() {
        return None;
    }
    Some((alg.to_string(), timestamp, ak, signature))
}

pub fn sign_actor_msg(msg_name: &str, msg_body: &str, key: &AkskKey<'_>) -> String {
    let body_hash = sha256_hex_lower(msg_body.as_bytes());
    let canonical = actor_msg_canonical(msg_name, &body_hash);
    let timestamp = chrono::Utc::now().format("%Y%m%dT%H%M%SZ").to_string();
    let sig_data = gen_signature_data(&canonical, &timestamp);
    let signature = hmac_sha256_hex(key.secret_key, &sig_data);
    format!(
        "HmacSha256 timestamp={timestamp},ak={},signature={signature}",
        key.access_key_id
    )
}

fn actor_msg_canonical(msg_name: &str, sha256_hex: &str) -> String {
    format!("{msg_name}\n{sha256_hex}")
}

pub fn verify_actor_msg(msg_name: &str, msg_body: &str, token: &str, key: &AkskKey<'_>) -> bool {
    let (alg, timestamp, ak, expect_sig) = match parse_auth_token(token) {
        Some(x) => x,
        None => return false,
    };
    if alg != "HmacSha256" || ak != key.access_key_id {
        return false;
    }
    let body_hash = sha256_hex_lower(msg_body.as_bytes());
    let canonical = actor_msg_canonical(msg_name, &body_hash);
    let actual = gen_signature_data(&canonical, &timestamp);
    hmac_sha256_hex(key.secret_key, &actual) == expect_sig
}

pub fn trans_to_enc_aksk_from_json(json: &str) -> EncAKSKContent {
    let mut out = EncAKSKContent {
        role: UNCONFIRMED_ROLE.to_string(),
        ..Default::default()
    };
    let v: serde_json::Value = match serde_json::from_str(json) {
        Ok(v) => v,
        Err(e) => {
            out.status = Status::new(
                StatusCode::ParameterError,
                format!("parse json failed, err: {e}"),
            );
            return out;
        }
    };
    if let Some(s) = v.get(TENANT_ID_STR).and_then(|x| x.as_str()) {
        out.tenant_id = s.to_string();
    }
    if let Some(s) = v.get(ACCESS_KEY_STR).and_then(|x| x.as_str()) {
        out.access_key = s.to_string();
    }
    if let Some(s) = v.get(SECRET_KEY_STR).and_then(|x| x.as_str()) {
        out.secret_key = s.to_string();
    }
    if let Some(s) = v.get(DATA_KEY_STR).and_then(|x| x.as_str()) {
        out.data_key = s.to_string();
    }
    if let Some(s) = v.get(EXPIRED_TIME_STAMP_STR).and_then(|x| x.as_str()) {
        match s.parse::<u64>() {
            Ok(u) => out.expired_time_stamp = u,
            Err(e) => {
                out.status = Status::new(
                    StatusCode::ParameterError,
                    format!("parse expiredTimeStamp failed, err: {e}"),
                );
                return out;
            }
        }
    }
    if let Some(s) = v.get(EXPIRED_TIME_SPAN_STR).and_then(|x| x.as_str()) {
        match s.parse::<u64>() {
            Ok(u) => out.expired_time_span = u,
            Err(e) => {
                out.status = Status::new(
                    StatusCode::ParameterError,
                    format!("parse expiredTimeSpan failed, err: {e}"),
                );
                return out;
            }
        }
    }
    if let Some(s) = v.get(ROLE_STR).and_then(|x| x.as_str()) {
        out.role = s.to_string();
    }
    out
}

pub fn trans_to_aksk_from_json(json: &str) -> AKSKContent {
    let mut out = AKSKContent {
        role: UNCONFIRMED_ROLE.to_string(),
        ..Default::default()
    };
    let v: serde_json::Value = match serde_json::from_str(json) {
        Ok(v) => v,
        Err(e) => {
            out.status = Status::new(
                StatusCode::ParameterError,
                format!("parse json failed, err: {e}"),
            );
            return out;
        }
    };
    if let Some(s) = v.get(TENANT_ID_STR).and_then(|x| x.as_str()) {
        out.tenant_id = s.to_string();
    }
    if let Some(s) = v.get(ACCESS_KEY_STR).and_then(|x| x.as_str()) {
        out.access_key = s.to_string();
    }
    if let Some(s) = v.get(SECRET_KEY_STR).and_then(|x| x.as_str()) {
        if let Ok(bytes) = hex::decode(s) {
            out.secret_key = bytes;
        }
    }
    if let Some(s) = v.get(DATA_KEY_STR).and_then(|x| x.as_str()) {
        if let Ok(bytes) = hex::decode(s) {
            out.data_key = bytes;
        }
    }
    if let Some(s) = v.get(ROLE_STR).and_then(|x| x.as_str()) {
        out.role = s.to_string();
    }
    out.expired_time_stamp = 0;
    out
}

pub fn trans_to_json_from_aksk(ak: &AKSKContent) -> String {
    let val = serde_json::json!({
        TENANT_ID_STR: ak.tenant_id,
        ACCESS_KEY_STR: ak.access_key,
        SECRET_KEY_STR: hex::encode(&ak.secret_key),
        DATA_KEY_STR: hex::encode(&ak.data_key),
    });
    val.to_string()
}

pub fn trans_to_permanent_cred_from_json(conf_json: &str) -> Vec<PermanentCredential> {
    if conf_json.is_empty() {
        return vec![];
    }
    let arr: serde_json::Value = match serde_json::from_str(conf_json) {
        Ok(v) => v,
        Err(_) => return vec![],
    };
    let Some(items) = arr.as_array() else {
        return vec![];
    };
    let mut out = Vec::new();
    for j in items {
        let mut c = PermanentCredential::default();
        if let Some(s) = j.get(TENANT_ID_STR).and_then(|x| x.as_str()) {
            c.tenant_id = s.to_string();
        }
        if c.tenant_id.is_empty() {
            continue;
        }
        if let Some(s) = j.get(CREDENTIAL_NAME_KEY_STR).and_then(|x| x.as_str()) {
            c.credential_name = s.to_string();
        }
        if let Some(s) = j.get(SERVICE_NAME_KEY_STR).and_then(|x| x.as_str()) {
            c.service_name = s.to_string();
        }
        if let Some(a) = j.get(MICROSERVICE_NAMES_KEY_STR).and_then(|x| x.as_array()) {
            for m in a {
                if let Some(s) = m.as_str() {
                    c.microservice_names.push(s.to_string());
                }
            }
        }
        out.push(c);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sign_and_verify_http_roundtrip() {
        let key = AkskKey {
            access_key_id: "ak",
            secret_key: b"defdf",
        };
        let mut queries = BTreeMap::new();
        queries.insert("q1".into(), "value1".into());
        queries.insert("q2".into(), "value2".into());
        queries.insert("q3".into(), "value3".into());
        let mut headers = BTreeMap::new();
        headers.insert("h1".into(), "value1".into());
        headers.insert("h2".into(), "value2".into());
        headers.insert("h3".into(), "value3".into());
        let body = r#"{"aaa":"bbb"}"#;
        let req = SignRequest::new("GET", "/getAAA", Some(queries.clone()), headers.clone(), body);
        let auth = sign_http_request(&req, &key);
        let signed = auth.get(HEADER_SIGNED_HEADER_KEY).unwrap();
        assert_eq!(signed, "h1;h2;h3");
        let mut signed_headers = headers;
        signed_headers.extend(auth);
        let verify_req = SignRequest::new("GET", "/getAAA", Some(queries), signed_headers, body);
        assert!(verify_http_request(&verify_req, &key));
    }

    #[test]
    fn sign_actor_roundtrip() {
        let key = AkskKey {
            access_key_id: "ak",
            secret_key: b"defdf",
        };
        let tok = sign_actor_msg("func1", "{xxxx:xxxx}", &key);
        assert!(verify_actor_msg("func1", "{xxxx:xxxx}", &tok, &key));
        assert!(!verify_actor_msg("func1", "{xxxx:xxx}", &tok, &key));
    }

    #[test]
    fn enc_aksk_json() {
        let j = r#"{"tenantID":"t1","accessKey":"a","secretKey":"s","dataKey":"d","expiredTimeStamp":"0"}"#;
        let e = trans_to_enc_aksk_from_json(j);
        assert!(e.status.is_ok());
        assert_eq!(e.tenant_id, "t1");
    }

    #[test]
    fn permanent_cred_array() {
        let j = r#"[{"tenantID":"t1","credentialName":"c","serviceName":"s","microserviceNames":["m"]}]"#;
        let v = trans_to_permanent_cred_from_json(j);
        assert_eq!(v.len(), 1);
        assert_eq!(v[0].microservice_names, vec!["m".to_string()]);
    }
}
