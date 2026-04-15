use tonic::Status;

#[derive(Debug, thiserror::Error)]
pub enum MetaStoreError {
    #[error("invalid argument: {0}")]
    InvalidArgument(String),
    #[error("permission denied: {0}")]
    PermissionDenied(String),
    #[error("decode: {0}")]
    Decode(#[from] prost::DecodeError),
    #[error("backup: {0}")]
    Backup(String),
    #[error("snapshot: {0}")]
    Snapshot(String),
}

impl From<MetaStoreError> for Status {
    fn from(e: MetaStoreError) -> Self {
        match e {
            MetaStoreError::InvalidArgument(m) => Status::invalid_argument(m),
            MetaStoreError::PermissionDenied(m) => Status::permission_denied(m),
            MetaStoreError::Decode(err) => Status::internal(err.to_string()),
            MetaStoreError::Backup(m) => Status::unavailable(m),
            MetaStoreError::Snapshot(m) => Status::internal(m),
        }
    }
}

impl MetaStoreError {
    pub(crate) fn into_status(self) -> Status {
        self.into()
    }
}
