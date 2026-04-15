use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("..")
        .join("..")
        .join("proto");

    let posix_dir = root.join("posix");
    let internal_dir = root.join("inner");

    let mut proto_files: Vec<PathBuf> = std::fs::read_dir(&posix_dir)?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().map_or(false, |ext| ext == "proto"))
        .collect();

    if internal_dir.exists() {
        let internal_protos: Vec<PathBuf> = std::fs::read_dir(&internal_dir)?
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| p.extension().map_or(false, |ext| ext == "proto"))
            .collect();
        proto_files.extend(internal_protos);
    }

    tonic_build::configure()
        .build_server(true)
        .build_client(true)
        .compile_protos(&proto_files, &[&posix_dir, &internal_dir])?;

    Ok(())
}
