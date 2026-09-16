# Local snapshot instance lifecycle after node failure

`failover=true` recovers a runtime from the original node's local recovery
candidate. Internal checkpoints are local artifacts even when reusable snapshot
storage uses a distributed backend. A confirmed scheduler/node failure terminates
that instance as `FATAL` and transfers its metadata to `InstanceManagerOwner`.
The master persists the instance and route together with a version check.
Local runtime recovery on a healthy owner remains supported.

## Failure and recovery boundaries

The existing scheduler heartbeat, busproxy registration and abnormal-node checks
establish a fault. A missing Kubernetes Pod name alone is not a deletion criterion.
A master restart rebuilds the instance and node views; a restored RUNNING record
whose owner is gone follows the same FATAL transition. Interrupted EVICTING and
EVICTED records follow this path too, including records whose original node never
registers with the new master. Legacy master-owned recovery-wait records also
converge to FATAL. An online owner or a still-valid proxy registration protects
its in-progress eviction from the periodic orphan scan.

A fault write operates on a copy. Until the authoritative metadata watch arrives,
the cached instance retains its previous generation. An unsuccessful transaction
must not expose an uncommitted FATAL version to garbage collection. After metadata
reconciliation, a queued fault write is discarded when the owner has recovered.

The proxy's local snapshot recovery commit checks the instance version and owner.
It therefore cannot overwrite a committed FATAL transition or versioned deletion.
A returning node reconciles its executor inventory against instances still owned
by that node; master-owned instances are excluded from that expected inventory.

## Deletion and garbage collection

Master-owned FATAL instances can be deleted without contacting the former owner.
The instance, route and optional debug record are deleted in one versioned
transaction. A response is successful only after the transaction succeeds;
storage failures and version conflicts are returned to the caller. At the storage layer, repeated
deletion of an absent record succeeds. A missing entry in the master's cache is
not sufficient evidence to acknowledge a route-less shutdown request. DELETE watches clean the instance lookup,
node index, master takeover index, route cache and existing lifecycle observers.

RUNNING local-only failover records can also be explicitly deleted when both the
scheduler node view and busproxy registration indicate that the owner is absent.
This includes records retained by older masters, without depending on their status
message. This path excludes system instances and accepts shutdown signals only.

The garbage collector scans canonical in-memory instance records across node and
master ownership. It first reconciles orphaned EVICTING/EVICTED, local-only
RUNNING failover and old node-owned FATAL records into persisted master-owned
FATAL records. It does not directly scan etcd; startup and metadata watches populate
this view. The scan interval remains five minutes and the age threshold one hour.
A valid createTimestamp supplies the age. Missing, malformed or future timestamps
use a persisted fatalTimestamp, so restarts do not reset the fallback retention
window. Expiration uses the versioned deletion transaction and protects newer
generations. The scan binds deletion directly to its captured key and version;
it does not re-resolve the ID through a queued general shutdown, or mark a newer
generation as exiting. System instances and drivers are excluded from this periodic scan.
Busproxy lease expiration and instance garbage collection remain separate mechanisms.

The frontend routes shutdown through a healthy control proxy when the original
owner is unavailable. That proxy forwards the authenticated tenant to the master;
the master validates tenant and instance identity before deletion. A repeated
request whose instance is absent from the cache succeeds only after authoritative
reads confirm that both the tenant's instance record and the route are absent.
Storage errors and unreconciled records remain errors. Other signal types retain
the owner-directed path.

## Metrics

- `yr_cluster_instance_total_count` and per-node `yr_instance_count` count
  canonical non-system RUNNING records with an available owner and no legacy
  recovery-wait marker. Old owner aliases cannot inflate the total.
- `yr_cluster_instance_recovering_count` counts legacy recovery-wait records once,
  including master takeover entries.
- `yr_cluster_instance_unavailable_count` counts terminal records and RUNNING
  records without an available owner. These are metadata records, not live VMs.

The exporter enabledInstruments allowlist must include both new gauge names;
the bundled metrics configuration includes them. Deployment-owned overrides,
including AKernel builder/config/metrics_config.json, must include them as well.

All three cluster gauges publish zero when their category is empty. Removed node
series receive a zero through the existing reporting path. Metrics reflect the
control-plane view and its failure-detection delay; physical sandbox inventory
remains a separate deployment verification source.

## Acceptance

Unit coverage includes FATAL persistence, version checks, master restart
reconciliation, live-owner protection, system-instance protection, idempotent
delete, transaction failure, index cleanup and metric classification. Deployment
acceptance must additionally exercise node removal, Pod rebuild, master restart,
real sandbox inventory, route cleanup and the published Prometheus samples.
