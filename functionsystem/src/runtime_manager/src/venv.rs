//! Python venv and Java runtime hints aligned with function runtime types.

use anyhow::Context;
use std::path::Path;
use std::process::Command;

/// If `runtime_type` looks like Python, ensure `.venv` under `code_root`.
pub fn prepare_python_env(code_root: &Path, runtime_type: &str) -> anyhow::Result<()> {
    let rt = runtime_type.to_ascii_lowercase();
    if !rt.contains("python") && rt != "py" {
        return Ok(());
    }
    let venv = code_root.join(".venv");
    if venv.join("bin").join("python").exists() || venv.join("bin").join("python3").exists() {
        return Ok(());
    }
    let _ = Command::new("python3")
        .args(["-m", "venv"])
        .arg(&venv)
        .current_dir(code_root)
        .status()
        .context("python3 -m venv")?;
    Ok(())
}

/// Returns extra env vars for Java-style runtimes (JAVA_HOME from system if unset).
pub fn java_env_hints(code_root: &Path, runtime_type: &str) -> Vec<(String, String)> {
    let rt = runtime_type.to_ascii_lowercase();
    if !rt.contains("java") && !rt.contains("jvm") {
        return Vec::new();
    }
    if std::env::var("JAVA_HOME").is_ok() {
        return Vec::new();
    }
    for candidate in ["/usr/lib/jvm/default-java", "/usr/lib/jvm/java-17-openjdk"] {
        let p = Path::new(candidate);
        if p.exists() {
            return vec![("JAVA_HOME".into(), p.to_string_lossy().into_owned())];
        }
    }
    let _ = code_root;
    Vec::new()
}
