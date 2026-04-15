//! Port of `functionsystem/src/common/meta_store_adapter/instance_operator.h`.

use crate::metadata::StoreInfo;
use crate::status::Status;
use async_trait::async_trait;

pub const TRANSACTION_ERROR_START: i32 = 300;
pub const TRANSACTION_ERROR_END: i32 = 350;

#[inline]
pub fn transaction_failed_for_etcd(err_code: i32) -> bool {
    err_code >= TRANSACTION_ERROR_START && err_code < TRANSACTION_ERROR_END
}

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PersistenceType {
    PersistentNot = 0,
    PersistentInstance = 1,
    PersistentRoute = 2,
    PersistentAll = 3,
}

#[derive(Debug, Clone, PartialEq)]
pub struct OperateResult {
    pub status: Status,
    pub value: String,
    pub pre_key_version: i64,
    pub current_mod_revision: i64,
}

impl OperateResult {
    pub fn new(status: Status) -> Self {
        Self {
            status,
            value: String::new(),
            pre_key_version: 0,
            current_mod_revision: 0,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct OperateInfo {
    pub key: String,
    pub values: Vec<String>,
    pub key_size: u64,
    pub version: i64,
    pub is_cent_os: bool,
    /// Serialized or opaque metastore transaction payload when available.
    pub response: Option<serde_json::Value>,
}

/// Instance CRUD against the metastore (etcd / txn backend).
#[async_trait]
pub trait InstanceOperator: Send + Sync {
    async fn create(
        &self,
        instance_info: StoreInfo,
        route_info: StoreInfo,
        is_low_reliability: bool,
    ) -> OperateResult;

    async fn modify(
        &self,
        instance_info: StoreInfo,
        route_info: StoreInfo,
        version: i64,
        is_low_reliability: bool,
    ) -> OperateResult;

    async fn delete(
        &self,
        instance_info: StoreInfo,
        route_info: StoreInfo,
        debug_inst_put_info: StoreInfo,
        version: i64,
        is_low_reliability: bool,
    ) -> OperateResult;

    async fn get_instance(&self, key: &str) -> OperateResult;

    async fn force_delete(
        &self,
        instance_info: StoreInfo,
        route_info: StoreInfo,
        debug_inst_put_info: StoreInfo,
        is_low_reliability: bool,
    ) -> OperateResult;
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::status::StatusCode;

    struct Noop;

    #[async_trait]
    impl InstanceOperator for Noop {
        async fn create(
            &self,
            _: StoreInfo,
            _: StoreInfo,
            _: bool,
        ) -> OperateResult {
            OperateResult::new(Status::ok())
        }
        async fn modify(
            &self,
            _: StoreInfo,
            _: StoreInfo,
            _: i64,
            _: bool,
        ) -> OperateResult {
            OperateResult::new(Status::ok())
        }
        async fn delete(
            &self,
            _: StoreInfo,
            _: StoreInfo,
            _: StoreInfo,
            _: i64,
            _: bool,
        ) -> OperateResult {
            OperateResult::new(Status::ok())
        }
        async fn get_instance(&self, _: &str) -> OperateResult {
            OperateResult::new(Status::ok())
        }
        async fn force_delete(
            &self,
            _: StoreInfo,
            _: StoreInfo,
            _: StoreInfo,
            _: bool,
        ) -> OperateResult {
            OperateResult::new(Status::ok())
        }
    }

    #[tokio::test]
    async fn noop_operator_ok() {
        let n = Noop;
        let r = n
            .get_instance("k")
            .await;
        assert!(r.status.is_ok());
    }

    #[test]
    fn txn_error_range() {
        assert!(transaction_failed_for_etcd(300));
        assert!(transaction_failed_for_etcd(349));
        assert!(!transaction_failed_for_etcd(350));
        assert!(!transaction_failed_for_etcd(299));
    }

    #[test]
    fn operate_result_with_error() {
        let s = Status::new(StatusCode::Failed, "x");
        let r = OperateResult {
            status: s,
            value: "v".into(),
            pre_key_version: 1,
            current_mod_revision: 2,
        };
        assert!(r.status.is_error());
    }
}
