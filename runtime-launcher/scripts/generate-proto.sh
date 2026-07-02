#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LAUNCHER_DIR="$ROOT_DIR/runtime-launcher"
OUT_DIR="$LAUNCHER_DIR/api/proto/runtime/v1"
PROTO_DIR="$ROOT_DIR/proto/posix"

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.pb.go

protoc \
  -I "$PROTO_DIR" \
  --go_out="$OUT_DIR" \
  --go_opt=paths=source_relative \
  --go_opt=Mruntime_launcher_interface.proto=runtime-launcher/api/proto/runtime/v1\;runtimev1 \
  "$PROTO_DIR/runtime_launcher_interface.proto" \
  "$PROTO_DIR/sandbox_api.proto"

protoc \
  -I "$PROTO_DIR" \
  --go-grpc_out="$OUT_DIR" \
  --go-grpc_opt=paths=source_relative \
  --go-grpc_opt=Mruntime_launcher_interface.proto=runtime-launcher/api/proto/runtime/v1\;runtimev1 \
  "$PROTO_DIR/sandbox_api.proto"
