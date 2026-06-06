# Autoscaling Subsystem Parity Matrix

Date: 2026-06-06
Branch: `rust-rewrite`
Oracle: `feature/sandbox`

## Scope

Depth-verification of the "autoscaling" subsystem from doc 169 (it was marked
"needs deep verification"). feature/sandbox autoscaling commits since merge-base:

```
8fcf349b !283 feat: add sandbox lifecycle observability metrics and autoscaling scheduling APIs
856b9d76 feat: add autoscaling scheduler observability APIs
36ec2ea7 enable vertical scale when no enough resource
ea66b604 feat(function): Set default values for AutoScaleConfig parameters
5439e5c0 support oneshot and autoscaler config
```

## Findings (per area)

| Area | Where it lives | Rust functionsystem | Status |
| --- | --- | --- | --- |
| `AutoScaleConfig` fields (SLAQuota, ScaleDownTime, BurstScaleNum) + defaults, oneshot/autoscaler config | **Go `meta_service`** (`meta_service/common/metadata/faasfunction.go`) — commits `ea66b604`, `5439e5c0` | not applicable | **N/A** — Go meta-service / frontend layer, not the C++/Rust functionsystem |
| Scaler / pool / system-function-pod / HPA | C++ `function_master/scaler/*` + `common/kube_client/*` (HPA, V1Deployment, apps/core v1 API) — large module new since merge-base | absent (no scaler/kube_client in Rust) | **Out of proven scope** — Kubernetes-deployment-specific; the ST source-replacement lane runs `-m process`, not k8s. Deferred by design, not a core behavioral gap |
| Autoscaling scheduling observability (`schedule_recorder`, domain_sched_srv / domain_group_ctrl APIs) — `856b9d76`/`8fcf349b` | C++ `domain_scheduler/.../schedule_recorder` | present analogue: `common/utils/src/schedule/recorder.rs` | **Has analogue — depth unverified** (minor) |
| "vertical scale when no enough resource" (`36ec2ea7`) | C++ scaler vertical-scale path | tied to scaler | **Out of proven scope** (with the scaler) |

## Conclusion

Unlike quota (doc 170, a real core gap), **autoscaling is largely N/A or out-of-scope for the
functionsystem rewrite**:

- Function-level autoscale config is a **Go meta_service** concern → not in functionsystem scope.
- The scaler/HPA/pool machinery is **Kubernetes-deployment-specific** (`kube_client`), while the
  proven Rust lane is process-mode single-shot ST → deferred by design, absent in Rust intentionally.
- Only the **scheduling-observability** slice maps into the Rust functionsystem, and a
  `schedule/recorder.rs` analogue already exists (depth unverified, low priority).

**Action: none for core parity.** Recommend marking autoscaling as N/A/out-of-scope in the doc-169
matrix, with a note that k8s-mode autoscaling (scaler + kube_client) is a separate, larger track if
k8s deployment ever becomes a rewrite goal.

## Meta-note for the audit

Not every feature/sandbox "subsystem" is in the functionsystem-rewrite scope. Two scope exclusions
recur: **Go meta_service/frontend** changes, and **Kubernetes-mode** machinery. The depth-verify
campaign should classify each subsystem's *home* (functionsystem vs Go vs k8s) before judging parity.
