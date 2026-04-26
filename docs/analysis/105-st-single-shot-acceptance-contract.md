# ST Single-Shot Acceptance Contract

## Decision

For Rust source-replacement acceptance, run the official ST script in one shot:

```bash
cd /workspace/clean_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "$FILTER"
```

Use the same command shape for the clean C++ control lane and the Rust replacement lane.

## Non-goal

Do not use this two-step sequence as an acceptance path:

```bash
bash test.sh -s -r
# export printed env
bash test.sh -b -l cpp
```

`test.sh -s -r` is debug-only. It starts a deployment and intentionally keeps it alive. A later
`test.sh -b -l cpp` invocation calls `deploy_yr` again; it does not reuse the reserved deployment.
That can create stale process, port, etcd, and datasystem interference that is outside the official
single-shot ST acceptance contract.

## Script semantics checked

The official `test.sh` flow is:

```bash
compile_st
clean_hook
deploy_yr
if [ "$START_ONLY" == "off" ]; then
    generate_test_dir
    run_st
fi
```

Therefore:

- `-s` sets `START_ONLY=on` and `RESERVED_CLUSTER=on`.
- `-r` keeps the deployment alive by disabling the normal exit cleanup hook.
- `-b -l cpp` still deploys before running cpp ST.

## Project rule

Current black-box source replacement proof must not patch upper-layer `yuanrong` ST scripts to make
Rust pass. If a reusable two-step workflow is needed later, add it as an upstream harness feature
such as `--reuse-existing` or `--no-deploy`; do not treat the current debug-only `-s -r` behavior as
canonical acceptance.
