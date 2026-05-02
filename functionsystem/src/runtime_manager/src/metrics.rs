use crate::config::Config;
use crate::container::detect_accelerators;
use crate::state::RuntimeManagerState;
use anyhow::Context;
use prometheus::{Encoder, Gauge, IntGauge, IntGaugeVec, Opts, Registry, TextEncoder};
use serde::Serialize;
use std::collections::{BTreeMap, HashSet};
use std::env;
use std::ffi::CString;
use std::fs;
use std::path::Path;
use std::process::Command;
use std::sync::{Arc, OnceLock};
use std::time::Instant;

#[derive(Debug, Clone, Serialize, Default)]
pub struct NodeMetricsSample {
    pub cpu_usage_ratio: f64,
    pub memory_total_kb: u64,
    pub memory_available_kb: u64,
    pub memory_used_ratio: f64,
    pub disk_total_bytes: u64,
    pub disk_avail_bytes: u64,
    pub disk_used_ratio: f64,
}

#[derive(Debug, Clone, Serialize)]
pub struct InstanceMetric {
    pub instance_id: String,
    pub runtime_id: String,
    pub pid: i32,
    pub rss_kb: u64,
    pub port: u16,
    pub net_rx_bytes: u64,
    pub net_tx_bytes: u64,
    pub resource_limits: BTreeMap<String, f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct MetricsSnapshot {
    pub node_id: String,
    pub node: NodeMetricsSample,
    pub instances: Vec<InstanceMetric>,
    pub accelerators: crate::container::AcceleratorSnapshot,
    #[serde(flatten)]
    pub resource_projection: ResourceProjection,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct ResourceProjection {
    pub capacity: BTreeMap<String, f64>,
    pub used: BTreeMap<String, f64>,
    pub allocatable: BTreeMap<String, f64>,
    /// C++ `ResourceLabelsCollector` equivalent in JSON form for Rust schedulers.
    pub labels: BTreeMap<String, String>,
    /// C++ vector-resource projection (for NUMA/GPU/NPU/disk shapes) without
    /// changing the existing scalar `resources` compatibility map.
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    pub vectors: BTreeMap<String, VectorResource>,
    /// Compatibility shortcut for existing Rust schedulers that parse
    /// C++-style scalar resources from a top-level `resources` object.
    pub resources: BTreeMap<String, ScalarResource>,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct VectorResource {
    pub values: BTreeMap<String, VectorCategory>,
    #[serde(
        rename = "heterogeneousInfo",
        skip_serializing_if = "BTreeMap::is_empty"
    )]
    pub heterogeneous_info: BTreeMap<String, String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub extensions: Vec<ResourceExtension>,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct ResourceExtension {
    pub disk: DiskContent,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct DiskContent {
    pub name: String,
    pub size: u64,
    #[serde(rename = "mountPoints")]
    pub mount_points: String,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct VectorCategory {
    pub vectors: BTreeMap<String, VectorValues>,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct VectorValues {
    pub values: Vec<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct ScalarResource {
    pub scalar: ScalarValue,
}

#[derive(Debug, Clone, Serialize)]
pub struct ScalarValue {
    pub value: f64,
}

pub struct MetricsCollector {
    last_cpu: Option<(Instant, CpuStat)>,
}

#[derive(Debug, Clone, Default)]
struct CpuStat {
    idle: u64,
    total: u64,
}

impl MetricsCollector {
    pub fn new() -> Self {
        Self { last_cpu: None }
    }

    fn read_proc_stat_cpu() -> anyhow::Result<CpuStat> {
        let line = fs::read_to_string("/proc/stat")
            .context("read /proc/stat")?
            .lines()
            .next()
            .context("empty /proc/stat")?
            .to_string();
        let mut parts = line.split_whitespace();
        let _cpu = parts.next();
        let mut nums = Vec::new();
        for p in parts {
            if let Ok(v) = p.parse::<u64>() {
                nums.push(v);
            }
        }
        if nums.len() < 4 {
            anyhow::bail!("unexpected /proc/stat cpu line");
        }
        let idle = nums[3] + nums.get(4).copied().unwrap_or(0);
        let total: u64 = nums.iter().sum();
        Ok(CpuStat { idle, total })
    }

    fn sample_cpu_usage_ratio(&mut self) -> f64 {
        let now = Instant::now();
        let cur = Self::read_proc_stat_cpu().unwrap_or_default();
        let ratio = self
            .last_cpu
            .as_ref()
            .and_then(|(t0, prev)| {
                if now.duration_since(*t0) < std::time::Duration::from_millis(100) {
                    return None;
                }
                let didle = cur.idle.saturating_sub(prev.idle);
                let dtotal = cur.total.saturating_sub(prev.total);
                if dtotal == 0 {
                    return None;
                }
                Some((1.0 - (didle as f64 / dtotal as f64)).clamp(0.0, 1.0))
            })
            .unwrap_or(0.0);
        self.last_cpu = Some((now, cur));
        ratio
    }

    fn meminfo_kb(keys: &[&str]) -> anyhow::Result<std::collections::HashMap<String, u64>> {
        let text = fs::read_to_string("/proc/meminfo").context("read /proc/meminfo")?;
        let mut m = std::collections::HashMap::new();
        for line in text.lines() {
            for k in keys {
                if line.starts_with(&format!("{k}:")) {
                    let v = line.split_whitespace().nth(1).and_then(|s| s.parse().ok());
                    if let Some(v) = v {
                        m.insert((*k).to_string(), v);
                    }
                }
            }
        }
        Ok(m)
    }

    fn sample_memory() -> (u64, u64, f64) {
        let Ok(map) = Self::meminfo_kb(&["MemTotal", "MemAvailable"]) else {
            return (0, 0, 0.0);
        };
        let total = *map.get("MemTotal").unwrap_or(&0);
        let avail = *map.get("MemAvailable").unwrap_or(&0);
        let used_ratio = if total > 0 {
            ((total.saturating_sub(avail)) as f64 / total as f64).clamp(0.0, 1.0)
        } else {
            0.0
        };
        (total, avail, used_ratio)
    }

    fn sample_disk_root() -> (u64, u64, f64) {
        let mut vfs: libc::statvfs = unsafe { std::mem::zeroed() };
        let root = CString::new("/").unwrap();
        let rc = unsafe { libc::statvfs(root.as_ptr(), &mut vfs) };
        if rc != 0 {
            return (0, 0, 0.0);
        }
        let frsize = vfs.f_frsize as u64;
        let blocks = vfs.f_blocks as u64;
        let bavail = vfs.f_bavail as u64;
        let total = blocks.saturating_mul(frsize);
        let avail = bavail.saturating_mul(frsize);
        let used_ratio = if total > 0 {
            ((total.saturating_sub(avail)) as f64 / total as f64).clamp(0.0, 1.0)
        } else {
            0.0
        };
        (total, avail, used_ratio)
    }

    fn cpu_capacity_from_host() -> f64 {
        std::thread::available_parallelism()
            .map(|n| n.get() as f64 * 1000.0)
            .unwrap_or(0.0)
    }

    fn net_bytes_for_pid(pid: i32) -> (u64, u64) {
        let path = format!("/proc/{pid}/net/dev");
        let Ok(text) = fs::read_to_string(path) else {
            return (0, 0);
        };
        let mut rx = 0u64;
        let mut tx = 0u64;
        for line in text.lines().skip(2) {
            let cols: Vec<&str> = line.trim().split_whitespace().collect();
            if cols.len() < 10 {
                continue;
            }
            if cols[0].starts_with("lo:") {
                continue;
            }
            let Ok(rxb) = cols[1].parse::<u64>() else {
                continue;
            };
            let Ok(txb) = cols[9].parse::<u64>() else {
                continue;
            };
            rx += rxb;
            tx += txb;
        }
        (rx, tx)
    }

    fn rss_kb_for_pid(pid: i32) -> u64 {
        let path = format!("/proc/{pid}/status");
        let Ok(text) = fs::read_to_string(path) else {
            return 0;
        };
        for line in text.lines() {
            if line.starts_with("VmRSS:") {
                if let Some(kb) = line.split_whitespace().nth(1).and_then(|s| s.parse().ok()) {
                    return kb;
                }
            }
        }
        0
    }

    pub fn collect(&mut self, state: &Arc<RuntimeManagerState>) -> MetricsSnapshot {
        let cpu_usage_ratio = self.sample_cpu_usage_ratio();
        let (memory_total_kb, memory_available_kb, memory_used_ratio) = Self::sample_memory();
        let (disk_total_bytes, disk_avail_bytes, disk_used_ratio) = Self::sample_disk_root();

        let mut instances = Vec::new();
        for pid in state.list_running_pids() {
            if let Some(rid) = state.runtime_id_for_pid(pid) {
                if let Some(p) = state.get_by_runtime(&rid) {
                    if Path::new(&format!("/proc/{pid}")).exists() {
                        let (net_rx_bytes, net_tx_bytes) = Self::net_bytes_for_pid(pid);
                        instances.push(InstanceMetric {
                            instance_id: p.instance_id.clone(),
                            runtime_id: p.runtime_id.clone(),
                            pid,
                            rss_kb: Self::rss_kb_for_pid(pid),
                            port: p.port,
                            net_rx_bytes,
                            net_tx_bytes,
                            resource_limits: p
                                .resources
                                .iter()
                                .map(|(k, v)| (k.clone(), *v))
                                .collect(),
                        });
                    }
                }
            }
        }

        let accelerators = detect_accelerators();
        let node = NodeMetricsSample {
            cpu_usage_ratio,
            memory_total_kb,
            memory_available_kb,
            memory_used_ratio,
            disk_total_bytes,
            disk_avail_bytes,
            disk_used_ratio,
        };
        let resource_projection = build_resource_projection(&state.config, &node, &instances);

        MetricsSnapshot {
            node_id: state.config.node_id.clone(),
            node,
            instances,
            accelerators,
            resource_projection,
        }
    }
}

pub fn build_resource_projection(
    cfg: &Config,
    node: &NodeMetricsSample,
    instances: &[InstanceMetric],
) -> ResourceProjection {
    let mut capacity = BTreeMap::new();
    let mut used = BTreeMap::new();

    let metrics_mode = cfg.metrics_collector_type.trim();
    let node_mode = metrics_mode.eq_ignore_ascii_case("node");
    let cpu_capacity = if node_mode {
        (MetricsCollector::cpu_capacity_from_host() - cfg.overhead_cpu).max(0.0)
    } else {
        cfg.proc_metrics_cpu.max(0.0)
    };
    let memory_capacity = if node_mode {
        ((node.memory_total_kb as f64 / 1024.0) - cfg.overhead_memory).max(0.0)
    } else {
        cfg.proc_metrics_memory.max(0.0)
    };

    capacity.insert("cpu".to_string(), cpu_capacity);
    capacity.insert("memory".to_string(), memory_capacity);

    let memory_used_mb = if node_mode {
        node.memory_total_kb
            .saturating_sub(node.memory_available_kb) as f64
            / 1024.0
    } else {
        instances.iter().map(|i| i.rss_kb as f64 / 1024.0).sum()
    };
    used.insert("cpu".to_string(), 0.0);
    used.insert("memory".to_string(), memory_used_mb);

    if node.disk_total_bytes > 0 {
        let total_gb = node.disk_total_bytes as f64 / 1024.0 / 1024.0 / 1024.0;
        let used_gb = node.disk_total_bytes.saturating_sub(node.disk_avail_bytes) as f64
            / 1024.0
            / 1024.0
            / 1024.0;
        capacity.insert("disk".to_string(), total_gb);
        used.insert("disk".to_string(), used_gb);
    }

    if let Ok(custom) = serde_json::from_str::<BTreeMap<String, f64>>(&cfg.custom_resources) {
        for (k, v) in custom {
            if v >= 0.0 {
                capacity.insert(k, v);
            }
        }
    }

    let mut vectors = BTreeMap::new();
    if cfg.numa_collection_enable {
        if let Some((numa_count, numa_vectors)) =
            build_numa_vectors(&cfg.node_id, &collect_numa_cpu_counts())
        {
            capacity.insert("NUMA".to_string(), numa_count);
            used.insert("NUMA".to_string(), 0.0);
            vectors.insert("NUMA".to_string(), numa_vectors);
        }
    }
    if let Some(disk_vectors) = build_disk_vectors(&cfg.node_id, &cfg.disk_resources) {
        vectors.insert("disk".to_string(), disk_vectors);
    }
    for (name, count, vector) in collect_xpu_vectors(cfg) {
        capacity.insert(name.clone(), count);
        used.insert(name.clone(), count);
        vectors.insert(name, vector);
    }

    let allocatable = capacity.clone();
    let resources = capacity
        .iter()
        .map(|(name, value)| {
            (
                name.clone(),
                ScalarResource {
                    scalar: ScalarValue { value: *value },
                },
            )
        })
        .collect();
    let labels = collect_node_labels(cfg);

    ResourceProjection {
        capacity,
        used,
        allocatable,
        labels,
        vectors,
        resources,
    }
}

pub fn build_numa_vectors(
    node_id: &str,
    cpu_counts: &[(u32, u32)],
) -> Option<(f64, VectorResource)> {
    if cpu_counts.is_empty() {
        return None;
    }
    let node_ids = cpu_counts
        .iter()
        .map(|(node, _)| *node as f64)
        .collect::<Vec<_>>();
    let cpu_millicores = cpu_counts
        .iter()
        .map(|(_, cpus)| (*cpus as f64) * 1000.0)
        .collect::<Vec<_>>();

    let mut values = BTreeMap::new();
    values.insert("ids".to_string(), vector_category(node_id, node_ids));
    values.insert("CPU".to_string(), vector_category(node_id, cpu_millicores));

    Some((
        cpu_counts.len() as f64,
        VectorResource {
            values,
            heterogeneous_info: BTreeMap::new(),
            extensions: Vec::new(),
        },
    ))
}

fn vector_category(node_id: &str, values: Vec<f64>) -> VectorCategory {
    VectorCategory {
        vectors: BTreeMap::from([(node_id.to_string(), VectorValues { values })]),
    }
}

pub fn build_disk_vectors(node_id: &str, disk_resources: &str) -> Option<VectorResource> {
    let raw = disk_resources.trim();
    if raw.is_empty() {
        return None;
    }
    let Ok(serde_json::Value::Array(disks)) = serde_json::from_str::<serde_json::Value>(raw) else {
        return None;
    };

    let mut sizes = Vec::new();
    let mut extensions = Vec::new();
    for disk in disks {
        let Some((name, size, mount_points)) = parse_disk_resource_entry(&disk) else {
            continue;
        };
        sizes.push(size as f64);
        extensions.push(ResourceExtension {
            disk: DiskContent {
                name,
                size,
                mount_points,
            },
        });
    }
    if sizes.is_empty() {
        return None;
    }

    Some(VectorResource {
        values: BTreeMap::from([("disk".to_string(), vector_category(node_id, sizes))]),
        heterogeneous_info: BTreeMap::new(),
        extensions,
    })
}

const NPU_COLLECT_MODES: &[&str] = &["count", "hbm", "sfmd", "topo", "all"];

pub fn build_gpu_vectors_from_ids(
    node_id: &str,
    ids: &[u32],
) -> Option<(String, f64, VectorResource)> {
    build_xpu_vectors(XpuVectorSpec {
        node_id,
        resource_type: "GPU",
        product_model: Some("cuda"),
        vendor: Some("nvidia.com"),
        ids,
        hbm: &[],
        used_hbm: &[],
        memory: &[],
        used_memory: &[],
        stream: &[],
        latency: &[],
        health: &[],
        partition: &[],
        dev_cluster_ips: &[],
    })
}

pub fn build_npu_count_vectors_from_ids(
    node_id: &str,
    ids: &[u32],
) -> Option<(String, f64, VectorResource)> {
    if ids.is_empty() {
        return None;
    }
    let defaults_hbm = vec![1000; ids.len()];
    let zeros = vec![0; ids.len()];
    let streams = vec![110; ids.len()];

    build_xpu_vectors(XpuVectorSpec {
        node_id,
        resource_type: "NPU",
        product_model: Some("Ascend"),
        vendor: Some("huawei.com"),
        ids,
        hbm: &defaults_hbm,
        used_hbm: &zeros,
        memory: &zeros,
        used_memory: &zeros,
        stream: &streams,
        latency: &zeros,
        health: &zeros,
        partition: &[],
        dev_cluster_ips: &[],
    })
}

pub fn build_npu_topology_vectors_from_json(
    node_id: &str,
    json: &str,
) -> Option<(String, f64, VectorResource)> {
    let serde_json::Value::Object(nodes) = serde_json::from_str::<serde_json::Value>(json).ok()?
    else {
        return None;
    };

    for config in nodes.values() {
        if let Some(config_node) = config.get("nodeName").and_then(|v| v.as_str()) {
            if config_node != node_id {
                continue;
            }
        }
        let number = config.get("number")?.as_u64()? as usize;
        let ids = json_u32_array(config.get("vDeviceIDs")?)?;
        let partition = json_string_array(config.get("vDevicePartition")?)?;
        if number == 0 || number != ids.len() || number != partition.len() {
            continue;
        }
        return build_xpu_vectors(XpuVectorSpec {
            node_id,
            resource_type: "NPU",
            product_model: None,
            vendor: None,
            ids: &ids,
            hbm: &[],
            used_hbm: &[],
            memory: &[],
            used_memory: &[],
            stream: &[],
            latency: &[],
            health: &[],
            partition: &partition,
            dev_cluster_ips: &[],
        });
    }
    None
}

struct XpuVectorSpec<'a> {
    node_id: &'a str,
    resource_type: &'a str,
    product_model: Option<&'a str>,
    vendor: Option<&'a str>,
    ids: &'a [u32],
    hbm: &'a [u32],
    used_hbm: &'a [u32],
    memory: &'a [u32],
    used_memory: &'a [u32],
    stream: &'a [u32],
    latency: &'a [u32],
    health: &'a [u32],
    partition: &'a [String],
    dev_cluster_ips: &'a [String],
}

fn build_xpu_vectors(spec: XpuVectorSpec<'_>) -> Option<(String, f64, VectorResource)> {
    if spec.ids.is_empty() {
        return None;
    }
    let mut values = BTreeMap::new();
    insert_u32_vector(&mut values, spec.node_id, "ids", spec.ids);
    insert_u32_vector(&mut values, spec.node_id, "HBM", spec.hbm);
    insert_u32_vector(&mut values, spec.node_id, "usedHBM", spec.used_hbm);
    insert_u32_vector(&mut values, spec.node_id, "memory", spec.memory);
    insert_u32_vector(&mut values, spec.node_id, "usedMemory", spec.used_memory);
    insert_u32_vector(&mut values, spec.node_id, "stream", spec.stream);
    insert_u32_vector(&mut values, spec.node_id, "latency", spec.latency);
    insert_u32_vector(&mut values, spec.node_id, "health", spec.health);

    let mut heterogeneous_info = BTreeMap::new();
    if let Some(vendor) = spec.vendor.filter(|s| !s.is_empty()) {
        heterogeneous_info.insert("vendor".to_string(), vendor.to_string());
    }
    if let Some(product_model) = spec.product_model.filter(|s| !s.is_empty()) {
        heterogeneous_info.insert("product_model".to_string(), product_model.to_string());
    }
    if !spec.partition.is_empty() {
        heterogeneous_info.insert("partition".to_string(), spec.partition.join(","));
    }
    if !spec.dev_cluster_ips.is_empty() {
        heterogeneous_info.insert(
            "dev_cluster_ips".to_string(),
            spec.dev_cluster_ips.join(","),
        );
    }

    let name = spec
        .product_model
        .filter(|s| !s.is_empty())
        .map(|model| format!("{}/{}", spec.resource_type, model))
        .unwrap_or_else(|| spec.resource_type.to_string());

    Some((
        name,
        spec.ids.len() as f64,
        VectorResource {
            values,
            heterogeneous_info,
            extensions: Vec::new(),
        },
    ))
}

fn insert_u32_vector(
    values: &mut BTreeMap<String, VectorCategory>,
    node_id: &str,
    key: &str,
    items: &[u32],
) {
    if items.is_empty() {
        return;
    }
    values.insert(
        key.to_string(),
        vector_category(node_id, items.iter().map(|v| *v as f64).collect()),
    );
}

fn collect_xpu_vectors(cfg: &Config) -> Vec<(String, f64, VectorResource)> {
    let mut out = Vec::new();
    if cfg.gpu_collection_enable {
        if let Some(gpu) = collect_gpu_vectors_from_nvidia_smi(&cfg.node_id) {
            out.push(gpu);
        }
    }

    let mode = cfg.npu_collection_mode.trim().to_ascii_lowercase();
    if !NPU_COLLECT_MODES.contains(&mode.as_str()) {
        return out;
    }
    let npu = if mode == "count" {
        build_npu_count_vectors_from_ids(&cfg.node_id, &collect_numeric_device_ids("davinci"))
    } else {
        fs::read_to_string(&cfg.npu_device_info_path)
            .ok()
            .and_then(|json| build_npu_topology_vectors_from_json(&cfg.node_id, &json))
    };
    if let Some(npu) = npu {
        out.push(npu);
    }
    out
}

fn collect_gpu_vectors_from_nvidia_smi(node_id: &str) -> Option<(String, f64, VectorResource)> {
    let output = Command::new("nvidia-smi")
        .args([
            "--query-gpu=index,name,memory.total,memory.used",
            "--format=csv,noheader,nounits",
        ])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    let mut ids = Vec::new();
    let mut product_model = None;
    let mut hbm = Vec::new();
    let mut used_hbm = Vec::new();
    for line in stdout.lines() {
        let columns = line.split(',').map(str::trim).collect::<Vec<_>>();
        if columns.len() < 4 {
            continue;
        }
        let Ok(id) = columns[0].parse::<u32>() else {
            continue;
        };
        ids.push(id);
        if product_model.is_none() && !columns[1].is_empty() {
            product_model = Some(columns[1].to_string());
        }
        if let Ok(total) = columns[2].parse::<u32>() {
            hbm.push(total);
        }
        if let Ok(used) = columns[3].parse::<u32>() {
            used_hbm.push(used);
        }
    }
    if ids.is_empty() {
        return None;
    }
    let zeros = vec![0; ids.len()];
    let streams = vec![110; ids.len()];
    build_xpu_vectors(XpuVectorSpec {
        node_id,
        resource_type: "GPU",
        product_model: product_model.as_deref().or(Some("cuda")),
        vendor: Some("nvidia.com"),
        ids: &ids,
        hbm: &hbm,
        used_hbm: &used_hbm,
        memory: &zeros,
        used_memory: &zeros,
        stream: &streams,
        latency: &zeros,
        health: &zeros,
        partition: &[],
        dev_cluster_ips: &[],
    })
}

fn collect_numeric_device_ids(prefix: &str) -> Vec<u32> {
    let mut ids = Vec::new();
    let Ok(entries) = fs::read_dir("/dev") else {
        return ids;
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let Some(name) = name.to_str() else {
            continue;
        };
        let Some(suffix) = name.strip_prefix(prefix) else {
            continue;
        };
        if let Ok(id) = suffix.parse::<u32>() {
            ids.push(id);
        }
    }
    ids.sort_unstable();
    ids.dedup();
    ids
}

fn json_u32_array(value: &serde_json::Value) -> Option<Vec<u32>> {
    let serde_json::Value::Array(items) = value else {
        return None;
    };
    let mut out = Vec::with_capacity(items.len());
    for item in items {
        out.push(item.as_u64()? as u32);
    }
    Some(out)
}

fn json_string_array(value: &serde_json::Value) -> Option<Vec<String>> {
    let serde_json::Value::Array(items) = value else {
        return None;
    };
    let mut out = Vec::with_capacity(items.len());
    for item in items {
        out.push(item.as_str()?.to_string());
    }
    Some(out)
}

fn parse_disk_resource_entry(disk: &serde_json::Value) -> Option<(String, u64, String)> {
    let name = disk.get("name")?.as_str()?.to_string();
    let size = parse_cpp_disk_size_gb(disk.get("size")?.as_str()?)?;
    let mount_points = disk.get("mountPoints")?.as_str()?.to_string();
    if !validate_cpp_disk_mount_path(&mount_points) {
        return None;
    }
    Some((name, size, mount_points))
}

fn parse_cpp_disk_size_gb(size: &str) -> Option<u64> {
    let digits = size.strip_suffix('G')?;
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    digits.parse().ok()
}

fn validate_cpp_disk_mount_path(path: &str) -> bool {
    const MAX_PATH_LEN: usize = 8192;
    path.len() <= MAX_PATH_LEN
        && path.len() >= 3
        && path.starts_with('/')
        && path.ends_with('/')
        && !path.contains("..")
        && path
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'/' | b'.'))
}

fn collect_numa_cpu_counts() -> Vec<(u32, u32)> {
    collect_numa_cpu_counts_from_root(Path::new("/sys/devices/system/node"))
}

pub fn collect_numa_cpu_counts_from_root(root: &Path) -> Vec<(u32, u32)> {
    let mut out = Vec::new();
    let Ok(entries) = fs::read_dir(root) else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let Some(name) = name.to_str() else {
            continue;
        };
        let Some(id) = name
            .strip_prefix("node")
            .and_then(|s| s.parse::<u32>().ok())
        else {
            continue;
        };
        let cpulist = entry.path().join("cpulist");
        let Ok(text) = fs::read_to_string(cpulist) else {
            continue;
        };
        let count = parse_cpu_list_count(text.trim());
        if count > 0 {
            out.push((id, count));
        }
    }
    out.sort_by_key(|(id, _)| *id);
    out
}

fn parse_cpu_list_count(s: &str) -> u32 {
    let mut count = 0u32;
    for part in s.split(',').map(str::trim).filter(|p| !p.is_empty()) {
        if let Some((start, end)) = part.split_once('-') {
            let (Ok(start), Ok(end)) = (start.parse::<u32>(), end.parse::<u32>()) else {
                continue;
            };
            if end >= start {
                count = count.saturating_add(end - start + 1);
            }
        } else if part.parse::<u32>().is_ok() {
            count = count.saturating_add(1);
        }
    }
    count
}

fn collect_node_labels(cfg: &Config) -> BTreeMap<String, String> {
    collect_node_labels_from_sources(
        env::var("INIT_LABELS").ok().as_deref(),
        env::var("NODE_ID").ok().as_deref(),
        env::var("HOST_IP").ok().as_deref(),
        &cfg.resource_label_path,
    )
}

pub fn collect_node_labels_from_sources(
    init_labels: Option<&str>,
    node_id: Option<&str>,
    host_ip: Option<&str>,
    resource_label_path: &Path,
) -> BTreeMap<String, String> {
    let mut labels = BTreeMap::new();

    if let Some(raw) = init_labels.filter(|s| !s.is_empty()) {
        if let Ok(serde_json::Value::Object(obj)) = serde_json::from_str::<serde_json::Value>(raw) {
            for (k, v) in obj {
                if let Some(s) = v.as_str() {
                    labels.insert(k, s.to_string());
                }
            }
        }
    }

    if let Some(v) = node_id.filter(|s| !s.is_empty()) {
        labels.insert("NODE_ID".to_string(), v.to_string());
    }
    if let Some(v) = host_ip.filter(|s| !s.is_empty()) {
        labels.insert("HOST_IP".to_string(), v.to_string());
    }

    if let Ok(text) = fs::read_to_string(resource_label_path) {
        for line in text.lines() {
            if let Some((k, v)) = parse_label_file_line(line) {
                labels.insert(k, v);
            }
        }
    }

    labels
}

fn parse_label_file_line(line: &str) -> Option<(String, String)> {
    let (key, raw_value) = line.split_once('=')?;
    if raw_value.len() < 2 || !raw_value.starts_with('"') || !raw_value.ends_with('"') {
        return None;
    }
    Some((
        key.to_string(),
        raw_value[1..raw_value.len() - 1].to_string(),
    ))
}

struct PromBundle {
    reg: Registry,
    node_cpu: Gauge,
    node_mem: Gauge,
    node_disk: Gauge,
    inst_rss: IntGaugeVec,
    inst_rx: IntGaugeVec,
    inst_tx: IntGaugeVec,
    accel_nvidia: IntGauge,
    accel_davinci: IntGauge,
}

static PROM: OnceLock<PromBundle> = OnceLock::new();

fn prom() -> &'static PromBundle {
    PROM.get_or_init(|| {
        let reg = Registry::new();
        let node_cpu = Gauge::with_opts(Opts::new(
            "yr_rm_node_cpu_usage_ratio",
            "Host CPU busy ratio from /proc/stat",
        ))
        .unwrap();
        let node_mem = Gauge::with_opts(Opts::new(
            "yr_rm_node_memory_used_ratio",
            "Host memory used ratio from /proc/meminfo",
        ))
        .unwrap();
        let node_disk = Gauge::with_opts(Opts::new(
            "yr_rm_node_disk_used_ratio",
            "Root filesystem used ratio",
        ))
        .unwrap();
        let inst_rss = IntGaugeVec::new(
            Opts::new(
                "yr_rm_instance_rss_bytes",
                "Runtime process RSS (approx from VmRSS)",
            ),
            &["node_id", "instance_id", "runtime_id"],
        )
        .unwrap();
        let inst_rx = IntGaugeVec::new(
            Opts::new(
                "yr_rm_instance_net_rx_bytes",
                "Sum of per-interface RX bytes for process network ns",
            ),
            &["node_id", "instance_id", "runtime_id"],
        )
        .unwrap();
        let inst_tx = IntGaugeVec::new(
            Opts::new(
                "yr_rm_instance_net_tx_bytes",
                "Sum of per-interface TX bytes for process network ns",
            ),
            &["node_id", "instance_id", "runtime_id"],
        )
        .unwrap();
        let accel_nvidia = IntGauge::with_opts(Opts::new(
            "yr_rm_accelerator_nvidia_devices",
            "Count of /dev/nvidia* nodes",
        ))
        .unwrap();
        let accel_davinci = IntGauge::with_opts(Opts::new(
            "yr_rm_accelerator_davinci_devices",
            "Count of /dev/davinci* nodes",
        ))
        .unwrap();
        reg.register(Box::new(node_cpu.clone())).ok();
        reg.register(Box::new(node_mem.clone())).ok();
        reg.register(Box::new(node_disk.clone())).ok();
        reg.register(Box::new(inst_rss.clone())).ok();
        reg.register(Box::new(inst_rx.clone())).ok();
        reg.register(Box::new(inst_tx.clone())).ok();
        reg.register(Box::new(accel_nvidia.clone())).ok();
        reg.register(Box::new(accel_davinci.clone())).ok();
        PromBundle {
            reg,
            node_cpu,
            node_mem,
            node_disk,
            inst_rss,
            inst_rx,
            inst_tx,
            accel_nvidia,
            accel_davinci,
        }
    })
}

/// Updates Prometheus gauges from a snapshot; clears stale per-instance label sets.
pub fn apply_prometheus_snapshot(snap: &MetricsSnapshot) {
    let p = prom();
    p.node_cpu.set(snap.node.cpu_usage_ratio);
    p.node_mem.set(snap.node.memory_used_ratio);
    p.node_disk.set(snap.node.disk_used_ratio);
    p.accel_nvidia.set(i64::from(snap.accelerators.nvidia));
    p.accel_davinci.set(i64::from(snap.accelerators.davinci));

    let mut seen = HashSet::new();
    for i in &snap.instances {
        let key = format!("{}|{}|{}", snap.node_id, i.instance_id, i.runtime_id);
        seen.insert(key.clone());
        p.inst_rss
            .with_label_values(&[
                snap.node_id.as_str(),
                i.instance_id.as_str(),
                i.runtime_id.as_str(),
            ])
            .set((i.rss_kb.saturating_mul(1024)) as i64);
        p.inst_rx
            .with_label_values(&[
                snap.node_id.as_str(),
                i.instance_id.as_str(),
                i.runtime_id.as_str(),
            ])
            .set(i.net_rx_bytes as i64);
        p.inst_tx
            .with_label_values(&[
                snap.node_id.as_str(),
                i.instance_id.as_str(),
                i.runtime_id.as_str(),
            ])
            .set(i.net_tx_bytes as i64);
    }

    // Remove series for instances that disappeared (best-effort: track prior run).
    static PREV: OnceLock<std::sync::Mutex<HashSet<String>>> = OnceLock::new();
    let prev = PREV.get_or_init(|| std::sync::Mutex::new(HashSet::new()));
    let mut prev_g = prev.lock().unwrap();
    for k in prev_g.clone().difference(&seen) {
        let parts: Vec<&str> = k.split('|').collect();
        if parts.len() == 3 {
            let _ = p
                .inst_rss
                .remove_label_values(&[parts[0], parts[1], parts[2]]);
            let _ = p
                .inst_rx
                .remove_label_values(&[parts[0], parts[1], parts[2]]);
            let _ = p
                .inst_tx
                .remove_label_values(&[parts[0], parts[1], parts[2]]);
        }
    }
    *prev_g = seen;
}

pub fn prometheus_text() -> String {
    let p = prom();
    let encoder = TextEncoder::new();
    let mut buf = Vec::new();
    if encoder.encode(&p.reg.gather(), &mut buf).is_ok() {
        String::from_utf8_lossy(&buf).into_owned()
    } else {
        String::new()
    }
}
