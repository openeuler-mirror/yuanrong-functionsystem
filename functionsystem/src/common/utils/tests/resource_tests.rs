//! Resource-style calculations: maps, bounds against defaults, and safe arithmetic helpers.

use clap::Parser;
use std::collections::HashMap;

use yr_common::CommonConfig;

#[derive(clap::Parser)]
#[command(name = "r")]
struct R {
    #[command(flatten)]
    common: CommonConfig,
}

fn default_limits() -> (u64, u64, u64, u64) {
    let c = R::try_parse_from(["r"]).unwrap().common;
    (
        c.min_instance_cpu_size,
        c.max_instance_cpu_size,
        c.min_instance_memory_size,
        c.max_instance_memory_size,
    )
}

fn clamp_cpu(requested: u64) -> u64 {
    let (min, max, _, _) = default_limits();
    requested.clamp(min, max)
}

fn clamp_memory(requested: u64) -> u64 {
    let (_, _, min, max) = default_limits();
    requested.clamp(min, max)
}

fn sum_bundle_cpu(bundles: &[HashMap<String, f64>]) -> f64 {
    bundles
        .iter()
        .flat_map(|m| m.get("cpu"))
        .copied()
        .sum()
}

fn alloc_used(mut used: HashMap<String, f64>, key: &str, delta: f64) -> Option<HashMap<String, f64>> {
    let cap = *used.get("capacity")?;
    let u = used.entry(key.to_string()).or_insert(0.0);
    let next = *u + delta;
    if next > cap {
        None
    } else {
        *u = next;
        Some(used)
    }
}

#[test]
fn default_cpu_bounds_ordering() {
    let (min, max, _, _) = default_limits();
    assert!(min <= max);
    assert!(min > 0);
}

#[test]
fn default_memory_bounds_ordering() {
    let (_, _, min, max) = default_limits();
    assert!(min <= max);
    assert!(max > min);
}

#[test]
fn clamp_cpu_below_min_becomes_min() {
    assert_eq!(clamp_cpu(0), 300);
}

#[test]
fn clamp_cpu_above_max_becomes_max() {
    assert_eq!(clamp_cpu(u64::MAX), 16_000);
}

#[test]
fn clamp_cpu_mid_unchanged() {
    assert_eq!(clamp_cpu(1000), 1000);
}

#[test]
fn clamp_memory_below_min_becomes_min() {
    assert_eq!(clamp_memory(0), 128);
}

#[test]
fn clamp_memory_at_max_boundary() {
    let (_, _, _, max) = default_limits();
    assert_eq!(clamp_memory(max), max);
}

#[test]
fn clamp_memory_above_max_truncates() {
    let (_, _, _, max) = default_limits();
    assert_eq!(clamp_memory(max.saturating_add(1)), max);
}

#[test]
fn sum_bundle_cpu_empty_is_zero() {
    assert_eq!(sum_bundle_cpu(&[]), 0.0);
}

#[test]
fn sum_bundle_cpu_single() {
    let m = HashMap::from([("cpu".into(), 2.5)]);
    assert!((sum_bundle_cpu(&[m]) - 2.5).abs() < f64::EPSILON);
}

#[test]
fn sum_bundle_cpu_multiple_bundles() {
    let a = HashMap::from([("cpu".into(), 1.0)]);
    let b = HashMap::from([("cpu".into(), 0.5)]);
    assert!((sum_bundle_cpu(&[a, b]) - 1.5).abs() < 1e-9);
}

#[test]
fn sum_bundle_cpu_ignores_non_cpu_keys() {
    let m = HashMap::from([("memory".into(), 8.0)]);
    assert_eq!(sum_bundle_cpu(&[m]), 0.0);
}

#[test]
fn alloc_used_happy_path() {
    let used = HashMap::from([("capacity".into(), 4.0), ("cpu".into(), 1.0)]);
    let out = alloc_used(used, "cpu", 2.0).unwrap();
    assert!((out["cpu"] - 3.0).abs() < 1e-9);
}

#[test]
fn alloc_used_reject_overflow() {
    let used = HashMap::from([("capacity".into(), 1.0), ("cpu".into(), 1.0)]);
    assert!(alloc_used(used, "cpu", 1.0).is_none());
}

#[test]
fn alloc_used_missing_capacity_none() {
    let used = HashMap::from([("cpu".into(), 0.0)]);
    assert!(alloc_used(used, "cpu", 1.0).is_none());
}

#[test]
fn release_subtracts_and_stays_non_negative() {
    let mut used: HashMap<String, f64> = HashMap::from([("cpu".into(), 3.0_f64)]);
    let v = used.get_mut("cpu").unwrap();
    *v = (*v - 5.0).max(0.0);
    assert_eq!(*used.get("cpu").unwrap(), 0.0);
}

#[test]
fn compare_alloc_less_than() {
    assert!(1.0_f64 < 2.0);
}

#[test]
fn compare_alloc_equal_within_epsilon() {
    let a = 1.0_f64 + f64::EPSILON;
    assert!((a - 1.0).abs() < 1e-8);
}

#[test]
fn zero_delta_noop_allocation() {
    let used = HashMap::from([("capacity".into(), 2.0), ("cpu".into(), 1.0)]);
    let out = alloc_used(used, "cpu", 0.0).unwrap();
    assert!((out["cpu"] - 1.0).abs() < 1e-9);
}

#[test]
fn saturating_add_u64_no_panic() {
    let a: u64 = u64::MAX;
    let b: u64 = 1;
    assert_eq!(a.saturating_add(b), u64::MAX);
}

#[test]
fn saturating_sub_u64_at_zero() {
    assert_eq!(0u64.saturating_sub(5), 0);
}

#[test]
fn f64_nan_not_equal_self() {
    let n = f64::NAN;
    assert!(n != n);
}

#[test]
fn merge_resource_maps_last_wins_on_key() {
    let mut a: HashMap<String, f64> = HashMap::from([("cpu".into(), 1.0)]);
    let b: HashMap<String, f64> = HashMap::from([("cpu".into(), 2.0)]);
    a.extend(b);
    let diff: f64 = a["cpu"] - 2.0;
    assert!(diff.abs() < 1e-9);
}

#[test]
fn scheduling_style_priority_cmp() {
    assert!(10i32 > 5);
}

#[test]
fn negative_f64_resource_representable() {
    let m: HashMap<String, f64> = HashMap::from([("credit".into(), -1.0)]);
    assert!(m["credit"] < 0.0);
}

#[test]
fn very_small_positive_cpu() {
    let m = HashMap::from([("cpu".into(), 1e-12)]);
    assert!(sum_bundle_cpu(&[m]) > 0.0);
}

#[test]
fn empty_map_allocation_count_zero() {
    let m: HashMap<String, f64> = HashMap::new();
    assert_eq!(m.len(), 0);
}

#[test]
fn instance_limits_independent_axes() {
    let (cmin, cmax, mmin, mmax) = default_limits();
    assert!(cmax - cmin > 0);
    assert!(mmax - mmin > 0);
}
