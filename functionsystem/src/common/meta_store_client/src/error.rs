use thiserror::Error;

#[derive(Debug, Error)]
pub enum MetaStoreError {
    #[error("etcd: {0}")]
    Etcd(#[from] etcd_client::Error),

    #[error("gRPC: {0}")]
    Grpc(#[from] tonic::Status),

    #[error("MetaStore client: {0}")]
    Msg(String),

    #[error("invalid configuration: {0}")]
    Config(String),
}

impl MetaStoreError {
    pub fn msg(s: impl Into<String>) -> Self {
        MetaStoreError::Msg(s.into())
    }
}

pub type Result<T> = std::result::Result<T, MetaStoreError>;
