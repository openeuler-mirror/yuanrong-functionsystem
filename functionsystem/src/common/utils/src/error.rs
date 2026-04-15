use thiserror::Error;

#[derive(Error, Debug)]
pub enum YrError {
    #[error("configuration error: {0}")]
    Config(String),

    #[error("etcd error: {0}")]
    Etcd(String),

    #[error("gRPC error: {0}")]
    Grpc(#[from] tonic::Status),

    #[error("serialization error: {0}")]
    Serialization(String),

    #[error("io error: {0}")]
    Io(#[from] std::io::Error),

    #[error("actor error: {0}")]
    Actor(String),

    #[error("internal error: {0}")]
    Internal(String),
}

pub type YrResult<T> = Result<T, YrError>;
