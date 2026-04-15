use thiserror::Error;

#[derive(Error, Debug)]
pub enum DsError {
    #[error("connection failed: {0}")]
    Connection(String),

    #[error("key not found: {0}")]
    NotFound(String),

    #[error("operation failed: {0}")]
    Operation(String),

    #[error("io error: {0}")]
    Io(#[from] std::io::Error),

    #[error("http error: {0}")]
    Http(#[from] reqwest::Error),

    #[error("serialization error: {0}")]
    Serialization(String),
}

pub type DsResult<T> = Result<T, DsError>;
