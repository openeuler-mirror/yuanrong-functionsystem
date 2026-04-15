/// Health / connectivity callbacks (C++ `MetaStoreHealthyObserver`).
pub trait MetaStoreHealthyObserver: Send + Sync {
    fn on_metastore_healthy_changed(&self, connected: bool);
}
