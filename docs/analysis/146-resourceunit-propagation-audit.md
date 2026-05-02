# ResourceUnit / Resource-View Propagation Audit

Date: 2026-05-02
Branch: `rust-rewrite`
Scope: Subgoal A from `docs/analysis/145-next-ai-handoff-blackbox-closure.md`

## Goal

Determine whether current Rust FunctionSystem propagates C++-equivalent `resources::ResourceUnit` through the live scheduling/resource-view path, or whether it still relies on lossy JSON/scalar shortcuts.

## C++ reference path

### Runtime-manager builds `resources::ResourceUnit`

C++ 0.8 `runtime_manager/metrics/metrics_actor.cpp` builds a protobuf `resources::ResourceUnit` and fills:

- system-level `capacity`
- system-level `allocatable`
- system-level `actualUse`
- per-instance `instances[*].actualUse`
- vector resources for GPU / NPU / DISK / NUMA
- `heterogeneousInfo`
- disk `extensions`
- `nodeLabels`

Relevant entry points:

- `MetricsActor::GetResourceUnit`
- `MetricsActor::BuildResourceUnit`
- `MetricsActor::BuildResourceUnitWithInstance`
- `MetricsActor::BuildResourceUnitWithSystem`
- `MetricsActor::BuildResource`
- `MetricsActor::BuildHeteroDevClusterResource`
- `MetricsActor::BuildDiskDevClusterResource`
- `MetricsActor::BuildNUMAResource`

### Runtime-manager sends `ResourceUnit` to function-agent

C++ `runtime_manager/manager/runtime_manager.cpp::RegisterToFunctionAgent` copies `metricsClient_->GetResourceUnit()` into `messages::RegisterRuntimeManagerRequest.resourceUnit`.

C++ `metrics_actor.cpp::BuildUpdateMetricsRequest` also sends `messages::UpdateResourcesRequest.resourceUnit`.

### Function-agent and resource-view keep `ResourceUnit` authoritative

C++ `function_agent/agent_service_actor.cpp` receives `messages::UpdateResourcesRequest.resourceUnit` and `RegisterRuntimeManagerRequest.resourceUnit`, stores it, and forwards resource updates to local scheduler/resource-view logic.

C++ `common/resource_view/` is built around `ResourceUnit` add/update/pull APIs rather than a scalar JSON map.

## Current Rust path

### Rust already has the protobuf types

Rust `functionsystem/src/common/proto/src/lib.rs` includes `yr_proto::resources`, and `proto/posix/resource.proto` defines the full C++ `ResourceUnit` shape.

So the gap is **not** missing generated protobuf types.

### Runtime-manager only exposes JSON projection today

Rust `functionsystem/src/runtime_manager/src/metrics.rs` builds `ResourceProjection` JSON with:

- scalar `capacity`
- scalar `used`
- scalar `allocatable`
- top-level `labels`
- vector `vectors`
- scalar compatibility map `resources`

This is useful for ST-facing projection parity, but it is **not** currently converted into a live `yr_proto::resources::ResourceUnit` for downstream scheduler/resource-view consumers.

### Live Rust scheduler contract is still JSON-only

Rust `proto/inner/scheduler.proto` currently uses:

- `RegisterRequest.resource_json`
- `UpdateResourcesRequest.resource_json`
- `ScheduleRequest.updated_resource_json`

There is no `resource_unit` field in the internal scheduler register/update path yet.

### Rust sender side is JSON-only

Current senders:

- `function_proxy/src/resource_reporter.rs`
- `function_proxy/src/registration.rs`
- `function_agent/src/registration.rs`

all emit only JSON payloads for resource propagation.

### Rust receiver side drops vector/resource-unit detail

Current receivers/storage:

- `function_master/src/topology.rs` stores only `resource_json`
- `function_master/src/resource_agg.rs` aggregates only scalar CPU/memory from JSON
- `domain_scheduler/src/resource_view.rs` parses only scalar `capacity` / `used`
- `domain_scheduler/src/nodes.rs` extracts labels from JSON, not from `ResourceUnit.nodeLabels`
- `function_proxy/src/resource_view.rs` tracks only scalar `cpu` / `memory` / `npu`

This means vector resources, per-instance actual use, and most `ResourceUnit` metadata do not survive into the authoritative Rust scheduling/resource-view path.

## Answering the required questions

### 1. Does Rust currently build `yr_proto::resources::ResourceUnit` equivalent to C++ for node capacity / actual / allocatable?

**No in the live path.**

Rust builds equivalent **data content** in `ResourceProjection` JSON, but it does not currently build and propagate a live `yr_proto::resources::ResourceUnit` through master/domain scheduler register/update flows.

### 2. Does Rust currently propagate vector resources, disk extensions, `heterogeneousInfo`, labels, and instance actual-use into scheduler/resource-view paths?

**Only partially, and mostly no.**

| Field class | Current Rust status | Notes |
| --- | --- | --- |
| Scalar capacity / used / allocatable | Partial | Preserved through JSON, but often reduced again to scalar maps |
| Node labels | Partial | Top-level JSON labels survive in some paths |
| Vector resources | No | Built in runtime metrics JSON, then dropped by master/domain resource parsing |
| Disk extensions | No | Built in runtime metrics JSON vectors, not consumed by master/domain resource path |
| `heterogeneousInfo` | No | Built in runtime metrics JSON vectors, not consumed downstream |
| Per-instance actual-use | No | C++ carries this in `ResourceUnit.instances[*].actualUse`; Rust report path emits only coarse JSON aggregates |

### 3. Which path is authoritative for scheduling today: JSON `ResourceProjection`, Rust-local `ResourceVector`, or protobuf `ResourceUnit`?

**Today the authoritative Rust path is JSON + scalar Rust-local views, not protobuf `ResourceUnit`.**

Concretely:

- proxy/master/domain registration uses `resource_json`
- master topology persists `resource_json`
- master scheduling hints aggregate scalar CPU/memory from JSON
- domain scheduler `ResourceView` stores scalar `capacity` / `used`
- proxy local `ResourceView` stores scalar `ResourceVector`

So current authority is:

1. JSON `resource_json` over the wire
2. scalar `ResourceUnit`/`ResourceVector`-like Rust structs in scheduler/resource-view code
3. **not** protobuf `yr_proto::resources::ResourceUnit`

### 4. Which C++ fields are absent or intentionally out of scope?

#### Clearly absent from the current Rust scheduling/resource-view path

- `ResourceUnit.fragment`
- `ResourceUnit.instances[*].actualUse` as protobuf resources
- `ResourceUnit.nodeLabels` counter form
- `Resource.systemInfo`
- `ResourceUnit.maxInstanceNum`
- `ResourceUnit.bucketIndexs`
- `ResourceUnit.revision`
- `ResourceUnit.status`
- `ResourceUnit.alias`
- `ResourceUnit.ownerId`
- `ResourceUnit.viewInitTime`
- vector `extensions`
- vector `heterogeneousInfo`

#### Likely out of scope for this slice unless a tested path requires them

- full C++ delta/revision semantics in `ResourceUnitChanges`
- complete fragment/bundle bookkeeping
- C++ resource-view lifecycle/state-machine parity beyond preserving authoritative resource content

## Audit conclusion

Current Rust black-box behavior is still **projection-first** rather than **ResourceUnit-first**:

- runtime-manager can describe C++-shaped resource data,
- but register/update propagation into master/domain scheduler collapses that data back into JSON/scalar forms,
- so vector resources and per-instance actual-use are not authoritative in the live scheduler/resource-view path.

## Required implementation direction

For Subgoal A, the minimum safe closure is:

1. keep existing `resource_json` compatibility,
2. add a Rust-authoritative `ResourceUnit` propagation path to internal scheduler register/update flows,
3. make master/domain resource views prefer that authoritative `ResourceUnit` path,
4. add tests proving scalar + vector + label + instance-use fields survive through the active Rust scheduling/resource path.
