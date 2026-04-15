//! In-memory MetaStore with etcd-compatible gRPC (KV / Watch / Lease / Maintenance status).
//!
//! Vendored `proto/*.proto` match etcd v3 `etcdserverpb` wire layout. Logical backup prefix
//! defaults to [`config::DEFAULT_KV_BACKUP_PREFIX`].

#![allow(clippy::large_enum_variant)]

mod backup;
mod config;
mod error;
mod kv_store;
mod lease_service;
mod lease_validator;
mod meta_store_grpc;
mod server;
mod snapshot_file;
mod watch_service;

pub mod pb {
    pub mod authpb {
        tonic::include_proto!("authpb");
    }
    pub mod mvccpb {
        tonic::include_proto!("mvccpb");
    }
    pub mod etcdserverpb {
        tonic::include_proto!("etcdserverpb");
    }
}

pub use config::{MetaStoreRole, MetaStoreServerConfig};
pub use error::MetaStoreError;
pub use kv_store::{KvState, KvStore, ValueEntry};
pub use server::MetaStoreServer;
pub use yr_metastore_client::MetaStoreClient;
