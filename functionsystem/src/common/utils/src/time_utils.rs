//! Port of `functionsystem/src/common/utils/time_utils.h`.

use chrono::{DateTime, NaiveDate, NaiveDateTime, NaiveTime, Utc};

/// Parse compact UTC timestamp `YYYYMMDDTHHMMSSZ` (same format as `litebus::time::GetCurrentUTCTime()`).
pub fn parse_timestamp(timestamp: &str) -> Option<DateTime<Utc>> {
    NaiveDateTime::parse_from_str(timestamp, "%Y%m%dT%H%M%SZ")
        .ok()
        .map(|nd| nd.and_utc())
}

/// Returns true if `timestamp1` is later than `timestamp2` by more than `seconds`.
pub fn is_later_than(timestamp1: &str, timestamp2: &str, seconds: f64) -> bool {
    let Some(t1) = parse_timestamp(timestamp1) else {
        return false;
    };
    let Some(t2) = parse_timestamp(timestamp2) else {
        return false;
    };
    let d = (t1 - t2).num_milliseconds() as f64 / 1000.0;
    d > seconds
}

/// Current UTC time in compact form used across signing and probes.
pub fn utc_compact_timestamp() -> String {
    Utc::now().format("%Y%m%dT%H%M%SZ").to_string()
}

/// Midnight UTC for a calendar date `YYYY-MM-DD`.
pub fn utc_midnight(date_yyyy_mm_dd: &str) -> Option<DateTime<Utc>> {
    let d = NaiveDate::parse_from_str(date_yyyy_mm_dd, "%Y-%m-%d").ok()?;
    let nd = NaiveDateTime::new(d, NaiveTime::from_hms_opt(0, 0, 0)?);
    Some(nd.and_utc())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_roundtrip_compact() {
        let s = "20250101T120000Z";
        let dt = parse_timestamp(s).unwrap();
        assert_eq!(dt.format("%Y%m%dT%H%M%SZ").to_string(), s);
    }

    #[test]
    fn is_later_than_one_hour() {
        let a = "20250101T130000Z";
        let b = "20250101T120000Z";
        assert!(is_later_than(a, b, 3500.0));
        assert!(!is_later_than(a, b, 4000.0));
    }

    #[test]
    fn utc_midnight_ok() {
        let m = utc_midnight("2025-06-01").unwrap();
        assert_eq!(m.format("%Y-%m-%dT%H:%M:%SZ").to_string(), "2025-06-01T00:00:00Z");
    }
}
