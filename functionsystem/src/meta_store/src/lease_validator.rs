use async_trait::async_trait;

#[async_trait]
pub trait LeaseValidator: Send + Sync {
    async fn valid_lease(&self, id: i64) -> bool;
}
