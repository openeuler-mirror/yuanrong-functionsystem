//! cgroup v2 memory accounting for runtime slices (C++ metrics memory collectors).

use std::fs;
use std::io;
use std::path::Path;

/// Reads `memory.current` and `memory.max` (bytes). `max` is `None` when unlimited (`max` file contains "max").
pub fn read_cgroup_memory(path: &Path) -> Option<(u64, Option<u64>)> {
    let cur_p = path.join("memory.current");
    let max_p = path.join("memory.max");
    let cur_s = fs::read_to_string(&cur_p).ok()?;
    let cur = cur_s.trim().parse::<u64>().ok()?;
    let max_s = fs::read_to_string(&max_p).ok()?;
    let max = max_s.trim();
    if max.eq_ignore_ascii_case("max") {
        Some((cur, None))
    } else {
        let m = max.parse::<u64>().ok()?;
        Some((cur, Some(m)))
    }
}

/// Set `memory.max` from a GiB limit (same sizing as `CgroupIsolate::apply`).
pub fn write_memory_max_from_gib(cgroup: &Path, mem_gib: f64) -> io::Result<()> {
    if mem_gib <= 0.0 {
        return Ok(());
    }
    let bytes = (mem_gib * 1024.0 * 1024.0 * 1024.0).ceil() as u64;
    let bytes = bytes.max(4 * 1024 * 1024);
    fs::write(cgroup.join("memory.max"), bytes.to_string())
}
