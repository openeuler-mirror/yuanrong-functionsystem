//! MetaStore client: etcd + optional MetaStore gRPC routing (C++ `MetaStoreClient` parity).

mod client;
mod config;
mod error;
mod health;
mod types;

pub use client::MetaStoreClient;
pub use config::{MetaStoreClientConfig, SslConfig};
pub use error::{MetaStoreError, Result as MetaStoreResult};
pub use health::MetaStoreHealthyObserver;
pub use types::{GetResponse, KeyValue, WatchEvent, WatchEventType};

pub use client::{CompareOp, Txn};

// etcd-native response / stream types (used by election, lease, health).
pub use etcd_client::{
    CampaignResponse, DeleteResponse as EtcdDeleteResponse, LeaderResponse, LeaseGrantResponse,
    LeaseKeepAliveResponse, LeaseKeepAliveStream, LeaseKeeper, LeaseRevokeResponse, ObserveStream,
    PutResponse as EtcdPutResponse, ResignResponse, StatusResponse, TxnResponse,
};
