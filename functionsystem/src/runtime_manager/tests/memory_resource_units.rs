//! Memory resource unit parity with C++ runtime-manager (MB, not GiB).

use yr_runtime_manager::container::memory_max_bytes_for_resource;
use yr_runtime_manager::oom::cgroup::write_memory_max_from_mb;

#[test]
fn cgroup_memory_limit_uses_cpp_megabyte_resource_units() {
    assert_eq!(
        memory_max_bytes_for_resource(Some(500.0)),
        Some(500 * 1024 * 1024)
    );
    assert_eq!(
        memory_max_bytes_for_resource(Some(128.0)),
        Some(128 * 1024 * 1024)
    );
    assert_eq!(memory_max_bytes_for_resource(Some(0.0)), None);
    assert_eq!(memory_max_bytes_for_resource(None), None);
}

#[test]
fn oom_cgroup_refresh_writes_megabytes_to_memory_max() {
    let dir = std::env::temp_dir().join(format!(
        "yr_rm_memory_units_{}_{}",
        std::process::id(),
        std::thread::current().name().unwrap_or("test")
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();

    write_memory_max_from_mb(&dir, 256.0).unwrap();

    let value = std::fs::read_to_string(dir.join("memory.max")).unwrap();
    assert_eq!(value, (256 * 1024 * 1024).to_string());

    let _ = std::fs::remove_dir_all(&dir);
}
