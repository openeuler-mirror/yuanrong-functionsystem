use anyhow::{anyhow, Context};
use async_trait::async_trait;
use flate2::read::GzDecoder;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fs::File;
use std::io::copy;
use std::path::{Path, PathBuf};
use tracing::info;
#[cfg(not(unix))]
use tracing::warn;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeployMode {
    Local,
    Copy,
    S3,
    SharedDir,
    Unknown,
}

impl DeployMode {
    pub fn from_proto(s: &str) -> Self {
        match s.trim().to_ascii_lowercase().as_str() {
            "" | "local" | "local_deployer" | "local_storage" => DeployMode::Local,
            "copy" | "copy_storage" | "copy_deployer" | "working_dir" | "working" => {
                DeployMode::Copy
            }
            "s3" | "obs" | "s3_storage" => DeployMode::S3,
            "shared" | "shared_dir" | "shared_dir_deployer" | "shared_storage" => {
                DeployMode::SharedDir
            }
            _ => DeployMode::Unknown,
        }
    }
}

pub struct DeployContext<'a> {
    pub instance_id: &'a str,
    pub function_name: &'a str,
    pub tenant_id: &'a str,
    pub code_uri: &'a str,
    pub deploy_mode: DeployMode,
    /// From URI fragment/query; verified after fetch/copy.
    pub checksum_sha256: Option<String>,
}

/// Split `#sha256=<64 hex>` or `?sha256=<64 hex>` from URI.
pub fn strip_uri_checksum(uri: &str) -> (String, Option<String>) {
    if let Some((base, query)) = uri.split_once('?') {
        let q = query.split_once('#').map(|(q, _)| q).unwrap_or(query);
        for pair in q.split('&') {
            if let Some(v) = pair.strip_prefix("sha256=") {
                if v.len() == 64 && v.chars().all(|c| c.is_ascii_hexdigit()) {
                    return (base.to_string(), Some(v.to_ascii_lowercase()));
                }
            }
        }
    }
    if let Some((base, frag)) = uri.split_once('#') {
        if let Some(v) = frag.strip_prefix("sha256=") {
            if v.len() == 64 && v.chars().all(|c| c.is_ascii_hexdigit()) {
                return (base.to_string(), Some(v.to_ascii_lowercase()));
            }
        }
        return (base.to_string(), None);
    }
    (uri.to_string(), None)
}

fn verify_sha256_file(path: &Path, expect: &str) -> anyhow::Result<()> {
    let mut f = File::open(path)?;
    let mut hasher = Sha256::new();
    copy(&mut f, &mut hasher)?;
    let got = hex::encode(hasher.finalize());
    if got != expect {
        anyhow::bail!("sha256 mismatch: expected {expect}, got {got}");
    }
    Ok(())
}

fn extract_tar_gz_if_needed(path: &Path) -> anyhow::Result<PathBuf> {
    let name = path.file_name().and_then(|s| s.to_str()).unwrap_or("");
    if !(name.ends_with(".tar.gz") || name.ends_with(".tgz")) {
        return Ok(path.to_path_buf());
    }
    let parent = path.parent().unwrap_or(Path::new("."));
    let dest_name = name
        .strip_suffix(".tar.gz")
        .or_else(|| name.strip_suffix(".tgz"))
        .unwrap_or("bundle");
    let out = parent.join(format!("{dest_name}_extracted"));
    if out.exists() {
        std::fs::remove_dir_all(&out).ok();
    }
    std::fs::create_dir_all(&out)?;
    let f = File::open(path)?;
    let dec = GzDecoder::new(f);
    let mut ar = tar::Archive::new(dec);
    ar.unpack(&out)?;
    Ok(out)
}

fn finalize_package_path(path: PathBuf, digest: Option<&str>) -> anyhow::Result<PathBuf> {
    if let Some(d) = digest {
        verify_sha256_file(&path, d)?;
    }
    extract_tar_gz_if_needed(&path)
}

#[async_trait]
pub trait Deployer: Send + Sync {
    async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf>;
}

pub struct LocalDeployer;

#[async_trait]
impl Deployer for LocalDeployer {
    async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf> {
        let p = PathBuf::from(ctx.code_uri);
        if !p.exists() {
            return Err(anyhow!("local code path does not exist: {}", p.display()));
        }
        if p.is_file() {
            finalize_package_path(p, ctx.checksum_sha256.as_deref())
        } else {
            Ok(p)
        }
    }
}

pub struct CopyDeployer {
    pub dest_root: PathBuf,
}

#[async_trait]
impl Deployer for CopyDeployer {
    async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf> {
        let src = PathBuf::from(ctx.code_uri);
        if !src.exists() {
            return Err(anyhow!("copy source missing: {}", src.display()));
        }
        let dst_dir = self.dest_root.join(ctx.instance_id);
        if dst_dir.exists() {
            std::fs::remove_dir_all(&dst_dir).ok();
        }
        std::fs::create_dir_all(&dst_dir)?;
        if src.is_dir() {
            copy_dir_all(&src, &dst_dir)?;
            return Ok(dst_dir);
        }
        let name = src
            .file_name()
            .ok_or_else(|| anyhow!("no file name in {}", src.display()))?;
        let df = dst_dir.join(name);
        std::fs::copy(&src, &df)?;
        finalize_package_path(df, ctx.checksum_sha256.as_deref())
    }
}

pub struct SharedDirDeployer {
    pub dest_root: PathBuf,
}

#[async_trait]
impl Deployer for SharedDirDeployer {
    async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf> {
        #[cfg(unix)]
        {
            use std::os::unix::fs::symlink;
            let target = PathBuf::from(ctx.code_uri);
            if !target.exists() {
                return Err(anyhow!("shared dir target missing: {}", target.display()));
            }
            let link = self.dest_root.join(ctx.instance_id);
            if link.exists() {
                std::fs::remove_file(&link).ok();
            }
            if let Some(parent) = link.parent() {
                std::fs::create_dir_all(parent)?;
            }
            symlink(&target, &link)?;
            Ok(link)
        }
        #[cfg(not(unix))]
        {
            warn!("symlink deploy unsupported on this platform; falling back to copy");
            CopyDeployer {
                dest_root: self.dest_root.clone(),
            }
            .deploy(ctx)
            .await
        }
    }
}

pub struct S3Deployer {
    pub endpoint: String,
    pub bucket: String,
    pub dest_root: PathBuf,
    pub client: reqwest::Client,
}

impl S3Deployer {
    fn object_url(&self, code_uri: &str) -> anyhow::Result<String> {
        let u = code_uri.trim();
        if u.starts_with("http://") || u.starts_with("https://") {
            return Ok(u.to_string());
        }
        if let Some(rest) = u.strip_prefix("s3://") {
            let rest = rest.trim_start_matches('/');
            let mut parts = rest.splitn(2, '/');
            let bucket = parts.next().unwrap_or("");
            let key = parts.next().unwrap_or("");
            if !self.bucket.is_empty() && bucket != self.bucket {
                return Err(anyhow!(
                    "s3 URI bucket {bucket} does not match configured bucket"
                ));
            }
            let b = if self.bucket.is_empty() {
                bucket
            } else {
                self.bucket.as_str()
            };
            let ep = self.endpoint.trim_end_matches('/');
            return Ok(format!("{ep}/{b}/{key}"));
        }
        if self.endpoint.is_empty() || self.bucket.is_empty() {
            return Err(anyhow!(
                "s3_endpoint and s3_bucket required when code_uri is not a full URL"
            ));
        }
        let key = u.trim_start_matches('/');
        Ok(format!(
            "{}/{}/{}",
            self.endpoint.trim_end_matches('/'),
            self.bucket.trim_start_matches('/'),
            key
        ))
    }
}

#[async_trait]
impl Deployer for S3Deployer {
    async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf> {
        let url = self.object_url(ctx.code_uri)?;
        let dst_dir = self.dest_root.join(ctx.instance_id);
        std::fs::create_dir_all(&dst_dir)?;
        let file_name = reqwest::Url::parse(&url)
            .ok()
            .and_then(|u| {
                u.path_segments()
                    .and_then(|mut s| s.next_back().map(|x| x.to_string()))
            })
            .filter(|s| !s.is_empty())
            .or_else(|| {
                Path::new(ctx.code_uri)
                    .file_name()
                    .and_then(|s| s.to_str())
                    .map(|s| s.to_string())
            })
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| "package.bin".to_string());
        let dst_file = dst_dir.join(&file_name);

        let bytes = self
            .client
            .get(&url)
            .send()
            .await
            .with_context(|| format!("GET {url}"))?
            .error_for_status()
            .with_context(|| format!("S3/OBS status {url}"))?
            .bytes()
            .await
            .with_context(|| format!("read body {url}"))?;
        std::fs::write(&dst_file, &bytes)?;
        info!(%url, path = %dst_file.display(), "downloaded code package");
        finalize_package_path(dst_file, ctx.checksum_sha256.as_deref())
    }
}

pub struct DeployRouter {
    pub dest_root: PathBuf,
    /// How many prior deploy roots to retain per `(function_name, tenant_id)` under `dest_root`.
    pub version_retention: usize,
    pub local: LocalDeployer,
    pub copy_d: CopyDeployer,
    pub shared: SharedDirDeployer,
    pub s3: Option<S3Deployer>,
}

impl DeployRouter {
    pub fn new(code_package_dir: PathBuf, s3_endpoint: String, s3_bucket: String) -> Self {
        let s3 = if !s3_endpoint.is_empty() && !s3_bucket.is_empty() {
            Some(S3Deployer {
                endpoint: s3_endpoint,
                bucket: s3_bucket,
                dest_root: code_package_dir.clone(),
                client: reqwest::Client::builder().build().expect("reqwest client"),
            })
        } else {
            None
        };
        Self {
            dest_root: code_package_dir.clone(),
            version_retention: 16,
            local: LocalDeployer,
            copy_d: CopyDeployer {
                dest_root: code_package_dir.clone(),
            },
            shared: SharedDirDeployer {
                dest_root: code_package_dir,
            },
            s3,
        }
    }

    pub async fn deploy(&self, ctx: DeployContext<'_>) -> anyhow::Result<PathBuf> {
        let mode = if ctx.deploy_mode == DeployMode::Unknown {
            if ctx.code_uri.starts_with("http://")
                || ctx.code_uri.starts_with("https://")
                || ctx.code_uri.starts_with("s3://")
            {
                DeployMode::S3
            } else if Path::new(ctx.code_uri).exists() {
                DeployMode::Local
            } else {
                ctx.deploy_mode
            }
        } else {
            ctx.deploy_mode
        };

        match mode {
            DeployMode::Local => self.local.deploy(ctx).await,
            DeployMode::Copy => self.copy_d.deploy(ctx).await,
            DeployMode::SharedDir => self.shared.deploy(ctx).await,
            DeployMode::S3 => {
                let Some(ref s3) = self.s3 else {
                    return Err(anyhow!(
                        "S3 deploy requested but s3_endpoint/s3_bucket are not configured"
                    ));
                };
                s3.deploy(ctx).await
            }
            DeployMode::Unknown => Err(anyhow!(
                "unknown deploy_mode and could not infer strategy (uri={})",
                ctx.code_uri
            )),
        }
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct VersionIndex {
    #[serde(default)]
    paths: Vec<String>,
}

/// Track deployed paths per `(function_name, tenant_id)` and delete older trees beyond `keep_last`.
pub fn record_and_prune_versions(
    dest_root: &Path,
    function_name: &str,
    tenant_id: &str,
    deployed: &Path,
    keep_last: usize,
) -> std::io::Result<()> {
    if keep_last == 0 {
        return Ok(());
    }
    let safe_fn: String = function_name
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    let safe_tn: String = tenant_id
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    let idx_dir = dest_root.join(".yr_pkg_versions");
    std::fs::create_dir_all(&idx_dir)?;
    let idx_path = idx_dir.join(format!("{safe_fn}__{safe_tn}.json"));
    let deployed_s = deployed.to_string_lossy().to_string();

    let mut idx: VersionIndex = if idx_path.exists() {
        let s = std::fs::read_to_string(&idx_path)?;
        serde_json::from_str(&s).unwrap_or_default()
    } else {
        VersionIndex::default()
    };

    idx.paths.retain(|p| p != &deployed_s);
    idx.paths.insert(0, deployed_s);

    let removed = if idx.paths.len() > keep_last {
        idx.paths.split_off(keep_last)
    } else {
        vec![]
    };

    std::fs::write(&idx_path, serde_json::to_string(&idx)?)?;

    for p in removed {
        let pb = PathBuf::from(&p);
        if pb == *deployed {
            continue;
        }
        if pb.exists() {
            if pb.is_dir() {
                let _ = std::fs::remove_dir_all(&pb);
            } else {
                let _ = std::fs::remove_file(&pb);
            }
        }
    }
    Ok(())
}

fn copy_dir_all(src: &Path, dst: &Path) -> anyhow::Result<()> {
    std::fs::create_dir_all(dst)?;
    for e in std::fs::read_dir(src)? {
        let e = e?;
        let ty = e.file_type()?;
        let s = e.path();
        let d = dst.join(e.file_name());
        if ty.is_dir() {
            copy_dir_all(&s, &d)?;
        } else {
            std::fs::copy(&s, &d)?;
        }
    }
    Ok(())
}
