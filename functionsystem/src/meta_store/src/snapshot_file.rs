//! Optional bincode snapshot of [`crate::kv_store::KvState`] for standalone / DR use.

use std::path::Path;

use crate::error::MetaStoreError;
use crate::kv_store::KvState;

pub fn load_kv_state(path: &Path) -> Result<KvState, MetaStoreError> {
    let f = std::fs::File::open(path).map_err(|e| MetaStoreError::Snapshot(e.to_string()))?;
    bincode::deserialize_from(f).map_err(|e| MetaStoreError::Snapshot(e.to_string()))
}

pub fn save_kv_state(path: &Path, st: &KvState) -> Result<(), MetaStoreError> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(|e| MetaStoreError::Snapshot(e.to_string()))?;
    }
    let tmp = path.with_extension("bin.tmp");
    {
        let f =
            std::fs::File::create(&tmp).map_err(|e| MetaStoreError::Snapshot(e.to_string()))?;
        bincode::serialize_into(f, st).map_err(|e| MetaStoreError::Snapshot(e.to_string()))?;
    }
    std::fs::rename(&tmp, path).map_err(|e| MetaStoreError::Snapshot(e.to_string()))?;
    Ok(())
}
