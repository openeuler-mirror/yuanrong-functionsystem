# Sandbox E2E Verification Runbook (run_docker_verify.sh + Rust source-replacement)

Date: 2026-06-07
Goal: black-box verify the Rust CONTAINER backend using the ready-made sandbox
acceptance `test/st/off_cluster/run_docker_verify.sh`, with the Rust functionsystem
swapped in (M7).

## What run_docker_verify.sh does (the pipeline)

1. **Compile** (in a `compile` container): `make frontend datasystem functionsystem
   runtime_launcher dashboard` + `bash build.sh -P` → builds the C++ control plane,
   the **Go runtime-launcher** (sandbox-shim), and the openyuanrong wheels.
2. **make image** → `deploy/sandbox/docker/build-images.sh` builds: base → compile →
   **controlplane** (`k8s/images/Dockerfile.controlplane-base`, which `pip install`s
   `openyuanrong-*.whl` — this is where functionsystem binaries enter) → runtime
   (`Dockerfile.runtime` → `aio-yr-runtime.tar`) → **aio-yr** (`Dockerfile.aio-yr`
   FROM controlplane, adds runtime-launcher + `CONTAINER_EP=unix:///var/run/
   runtime-launcher.sock` + in-container dockerd/containerd).
3. **Start AIO**: `deploy/sandbox/docker/run.sh` → `docker compose up` the `aio-yr`
   container (**privileged, cgroup:host**). supervisord runs: traefik, runtime-launcher,
   seed-traefik-etcd, yuanrong-master (start-yuanrong.sh, with CONTAINER_EP).
4. **pytest** `test_yrcli_sandbox_access_paths.py` against `YR_SERVER_ADDRESS=127.0.0.1:
   AIO_PORT`: `test_yrcli_sandbox_create_image_and_port_forwarding`,
   `test_yrcli_sandbox_create_reverse_tunnel` — real `yrctl sandbox create --image`
   with port-forward / reverse-tunnel.

So this single harness **provisions the whole sandbox env (functionsystem +
runtime-launcher + containerd) AND runs the e2e acceptance** — it is exactly the M7
environment + test we needed, not something to build from scratch.

## Where the Rust functionsystem is source-replaced

functionsystem binaries enter the AIO via the **openyuanrong wheel** baked into the
controlplane image (`Dockerfile.controlplane-base`: `COPY openyuanrong-*.whl` +
`pip install`). Two replacement strategies:

- **A (clean, image-baked):** before `build-images.sh`, replace the functionsystem
  binaries in the openyuanrong output/wheel with the Rust ones (the same
  source-replacement as the cpp ST: drop Rust `function_master/proxy/agent/...` into
  `output/openyuanrong/functionsystem/bin`), re-run `build.sh -P` to repackage the
  wheel, then `build-images.sh`. The AIO then runs Rust functionsystem.
- **B (fast, runtime swap):** after the AIO is up, `docker cp` the Rust binaries into
  the running `aio-yr` container's functionsystem bin and restart supervisord's
  `yuanrong-master`. Quicker iteration; less reproducible.

## Hard prerequisites / risks (must resolve before this works)

1. **M5 hot-path wiring MUST be done first.** `yrctl sandbox create` sends
   StartInstance with `type=CONTAINER(1)`. The Rust `runtime_ops::start_instance_op`
   does not yet branch to the SandboxExecutor (M5 decision logic exists, but the
   actual dispatch + param extraction from `RuntimeInstanceInfo.container` is not
   wired). Without it, container requests won't reach the Rust SandboxExecutor.
   → Complete the M5 wiring before any e2e is meaningful.
2. **Architecture: the AIO chain is amd64-oriented.** `Dockerfile.aio-yr` hardcodes
   `traefik_<v>_linux_amd64.tar.gz`; base/compile images are x86. On this Apple
   Silicon (arm64) host that means **amd64 emulation (Rosetta)** — which is the exact
   path that caused the early `deploy retry fail` problems (see the 0522 handoff). The
   proven cpp-ST lane is **native arm64**; the sandbox AIO does NOT inherit that. So
   either (a) run the AIO under amd64 emulation and accept the risk, or (b) arm64-adapt
   the chain (arm64 traefik url, arm64 base/compile/runtime images, arm64 runtime-launcher).
3. **Network.** The build pulls base images, apt packages, Go modules (runtime-launcher),
   traefik release, and vendor deps. This machine's outbound (gitcode/huaweicloud/github)
   is **intermittent** — same blocker seen all session. A stable proxy is advisable.
4. **A `compile` container** with the full toolchain and the repo mounted at
   `REPO_ROOT_IN_CONTAINER` is required (script expects `COMPILE_CONTAINER=compile`,
   user `wyc`). The existing `yr080-arm64-rustfs-compile` may or may not match the
   expected paths/user; verify or set `COMPILE_CONTAINER`/`REPO_ROOT_IN_CONTAINER`.
5. **Heavy build:** full C++ stack + Go + multiple Docker images. Long, and `build.sh -P`
   has the `go install @latest` / Bazel-server pitfalls noted in the yr-dev skill.

## Recommended sequence

1. Finish **M5 hot-path wiring** in Rust (CONTAINER branch + param extraction) — this is
   pure Rust, unit-testable, and is the real prerequisite for the e2e to exercise our code.
2. Decide the arch approach: amd64-emulated AIO (fast to try, Rosetta risk) vs arm64-adapt
   the AIO chain (more work, matches the proven native-arm64 lane).
3. Get one **C++-baseline** AIO running green first (`run_docker_verify.sh` unmodified) —
   establishes the env + a control (mirrors the "C++ baseline before Rust" discipline).
4. Then source-replace Rust (strategy A), re-run, and diff the sandbox e2e results vs the
   C++ baseline. That is the black-box parity proof for the CONTAINER backend.

## Flags useful for iteration

`run_docker_verify.sh --skip-compile --skip-image --skip-start -- -k port_forwarding`
reuses a running AIO and runs only a filtered pytest — fast loop once the env is up.
