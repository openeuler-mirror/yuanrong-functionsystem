#[tokio::main]
async fn main() -> anyhow::Result<()> {
    yr_iam::run().await
}
