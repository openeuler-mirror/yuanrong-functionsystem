<!--
Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
Licensed under the Apache License, Version 2.0.
See the LICENSE file in this repository for the complete license text.
-->

# Pause/Checkpoint 收敛实施计划

## 阶段 A：合入阻断项

1. 在 `snap_ctrl_actor_test.cpp` 增加 GetClient 永久失败后 Delete 的失败测试。
2. 增加 PrepareSnap transport 永久失败后 Delete 的失败测试。
3. 增加 operation deadline、stale timer 和 exactly-once 测试。
4. 在 `PauseContext` 中实现 phase、generation、deadline 和有上限指数退避。
5. 让 Delete 在 checkpoint 前取消 Prepare 并完成 terminal。
6. 在 `runtime_manager_test.cpp` 增加 SANDBOXD、unsupported、missing 和 reconcile 测试。
7. 实现返回 Status 的权威 executor 解析，删除 Pause 强制改道。
8. 在 `instance_operator_test.cpp` 增加空 route 不删除 key 的测试。
9. 回退 `Modify(empty route)=Delete`，验证 PAUSED route 仍为非空控制路由。
10. 运行定向测试和完整 `pause_resume_unit_test`。

## 阶段 B：统一物理 Checkpoint

1. 为 FunctionSystem 内部定义 CheckpointPlan 和 CheckpointResult。
2. 将 PauseArtifactManager 收敛为无节点私有 journal 的 PauseArtifactPathManager。
3. 在进入 RuntimeManager 前解析确定 checkpointID/checkpointDir。
4. 合并 SandboxdExecutor 的 TakeSnapshot/CheckpointLocal 物理调用。
5. 保留普通 snapshot 的 CkptFileManager 兼容 adapter。
6. 删除空 checkpointDir 哨兵；SandboxdExecutor 只在 checkpoint 完成后按
   USER_MANAGED/INSTANCE_MANAGED 选择发布策略，不另建 Pause 物理调用。
7. 增加普通 snapshot/Pause 共用原语、路径越界和 replay 测试。

## 阶段 C：统一 artifact 发布

1. 从 AgentServiceActor 抽取 SnapshotArtifactPublisher。
2. 让 Agent 以启动配置为存储后端权威，不接受 RuntimeManager 反向声明；
   USER_MANAGED 和 INSTANCE_MANAGED 仅表达制品生命周期策略。
3. 统一本地校验、PutTemporary、Publish、Stat、冲突和 exact Delete。
4. 为 OBS 与 DataSystem 增加同 digest replay、冲突和响应丢失测试。
5. 普通 snapshot 回归全绿后再移除 sandboxd snapshot 对 CkptFileManager 的直接耦合。

## 阶段 D：Pause 状态机与收敛

1. 将 pauseContexts_ 演进为显式 per-instance phase 状态机。
2. 用一次 Delete 接管事件替换 Register/Release intent 逻辑锁。
3. 引入 COMMITTED、SOURCE_RUNNING、RESULT_UNKNOWN、IDENTITY_CHANGED、
   TERMINAL_FAILURE 收敛结果。
4. 覆盖 Delete 在 Gate、Prepare、Checkpoint、Publish、Release、CAS 和 Finalize
   阶段到达的竞争测试。
5. 覆盖 actor shutdown、Proxy/Agent/Master 重启和 stale callback。

## 阶段 E：验证和交付

1. FunctionSystem 定向 LLT、pause_resume_unit_test、monolithic LLT 全绿。
2. RRT standalone 运行普通 snapshot、Pause/Delete、DataSystem、OBS 和循环场景。
3. 北京四集群运行同节点、跨节点、资源不足成功/失败和并发 Resume。
4. 保存 red/green/full JSON、ETCD watch、sandboxd 和组件关键日志。
5. 更新正式设计文档及 PR review comment 状态。
6. 最终将 FunctionSystem 整理为一个提交，YuanRong 主仓保留代码/文档提交和
   gitlink 提交；推送新的私仓分支并保持 PR 为 WIP。
