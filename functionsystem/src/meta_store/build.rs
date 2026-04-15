fn main() -> Result<(), Box<dyn std::error::Error>> {
    let proto = std::path::Path::new("proto");
    tonic_build::configure()
        .build_server(true)
        .build_client(false)
        .compile_protos(
            &[
                proto.join("auth.proto"),
                proto.join("kv.proto"),
                proto.join("rpc.proto"),
            ],
            &[proto],
        )?;
    Ok(())
}
