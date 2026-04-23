# Traefik HTTP Provider: Standby-to-Leader Forwarding Design

## Problem

When FunctionMaster runs in active-standby mode behind a LoadBalancer, Traefik's HTTP provider may hit the standby instance. Since only the leader has a complete, authoritative routing table (updated via MetaStore watch callbacks), the standby's local TraefikRouteCache may be stale or empty, causing:

- **Route flapping**: Traefik alternates between leader's full config and standby's partial/empty config.
- **Transient 404s**: Requests hit backends that exist only in one copy of the routing table.

## Design

### Core Idea

The standby FunctionMaster **forwards** `/traefik/config` requests to the leader, acting as a transparent proxy. Traefik always gets the leader's authoritative config regardless of which FunctionMaster the LB selects.

### Architecture

```
Traefik ──poll──► LB ──► FunctionMaster (standby)
                           │  isLeader=false
                           │  forward GET /global-scheduler/traefik/config
                           ▼
                         FunctionMaster (leader)
                           │  isLeader=true
                           │  serve from local TraefikRouteCache
                           ▼
                         JSON response
```

### Components

#### 1. TraefikLeaderContext (shared state)

A thread-safe struct holding:
- `isLeader` (atomic bool): fast-path check in the HTTP handler.
- `selfHttpAddress` (string, immutable after init): this instance's `ip:port`.
- `leaderHttpAddress` (string, guarded by shared_mutex): current leader's `ip:port`.

Updated by Explorer's `AddLeaderChangedCallback` whenever the leader changes.

#### 2. TraefikApiRouterRegister (HTTP handler)

On each `GET /traefik/config`:

1. **Leader path** (`isLeader == true`): serve JSON from local `TraefikRouteCache`. Zero overhead.
2. **Standby path** (`isLeader == false`):
   - Read `leaderHttpAddress` under shared lock.
   - If empty or equals self → return **503** (no leader known / self-loop guard).
   - Forward via `litebus::http::Get` with configurable timeout.
   - On success → proxy the leader's response body.
   - On failure (timeout, connection refused, non-200) → return **503**.

Returning 503 (not empty JSON) ensures Traefik keeps the previous config via its hash-based change detection (FNV hash unchanged → config retained).

#### 3. Explorer Callback Wiring (main.cpp)

After `InitGlobalSchedDriver` succeeds:
- **Standalone mode**: set `isLeader = true` immediately (no election).
- **Cluster mode**: register `"TraefikProvider"` callback with Explorer. On leader change, call `TraefikLeaderContext::UpdateLeader(addr, isSelf)`.

### Configuration

| Flag | Default | Constraint | Description |
|------|---------|------------|-------------|
| `traefik_forward_timeout_ms` | 3000 | 500–10000, must be < Traefik's `pollTimeout` (default 5s) | Timeout for standby→leader HTTP forward |

The timeout must be strictly less than Traefik's `providers.http.pollTimeout` (default 5s), otherwise Traefik will time out before the forward completes.

### Deployment Changes

| File | Change |
|------|--------|
| `functionsystem/scripts/deploy/function_system/install.sh` | Pass `--traefik_forward_timeout_ms` to `function_master` |
| `deploy/process/config.sh` | Add `TRAEFIK_FORWARD_TIMEOUT_MS` to getopt, defaults, parse, export |
| `akernel/builder/scripts/master_entrypoint.sh` | Add `--traefik_forward_timeout_ms=3000` to startup command |

### Failure Modes

| Scenario | Behavior |
|----------|----------|
| Leader healthy, standby receives poll | Forward succeeds, Traefik gets leader's config |
| Leader down, standby receives poll | Forward times out → 503 → Traefik keeps previous config |
| No leader elected yet | `leaderHttpAddress` empty → 503 → Traefik keeps previous config |
| Both instances are leaders (split-brain) | Each serves own cache; unlikely with proper election TTL |
| Forward timeout exceeds Traefik pollTimeout | Traefik times out first → keeps previous config (degraded but safe) |

### Why 503 Instead of Empty JSON

Traefik's HTTP provider uses FNV hash-based change detection. If the standby returned `{"http":{"routers":{},"services":{}},"tcp":{}}`, it would hash differently from the leader's config, causing Traefik to apply it and wipe all routes. Returning 503 (non-200) causes Traefik to treat the poll as a network error, triggering its exponential backoff retry while keeping the last-known-good config.

### Why Not Use the Standby's Local Cache

Both leader and standby receive MetaStore watch events and update their local `TraefikRouteCache`. However:
1. Watch propagation has latency — the standby may lag behind.
2. During leader failover, the new leader rebuilds state from MetaStore, but the old standby's cache may have already diverged.
3. Forwarding to the leader guarantees a single source of truth.
