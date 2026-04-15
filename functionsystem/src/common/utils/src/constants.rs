//! Ports of `common/constants/*.h` (actor names, signals, core scalars, executor type).

/// `common/constants/actor_name.h`
pub mod actor_name {
    pub const FUNCTION_PROXY_OBSERVER_ACTOR_NAME: &str = "FunctionProxyObserverActor";
    pub const REQUEST_ROUTER_NAME: &str = "RequestRouterActor";

    pub const LOCAL_SCHED_SRV_ACTOR_NAME: &str = "LocalSchedSrvActor";
    pub const LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX: &str = "-LocalSchedInstanceCtrlActor";
    pub const LOCAL_SCHED_FUNC_AGENT_MGR_ACTOR_NAME_POSTFIX: &str = "-LocalSchedFuncAgentMgrActor";
    pub const LOCAL_GROUP_CTRL_ACTOR_NAME: &str = "LocalGroupCtrlActor";
    pub const BUNDLE_MGR_ACTOR_NAME: &str = "BundleMgrActor";
    pub const SUBSCRIPTION_MGR_ACTOR_NAME_POSTFIX: &str = "-SubscriptionMgrActor";

    pub const GLOBAL_SCHED_ACTOR_NAME: &str = "GlobalSchedActor";
    pub const DOMAIN_SCHED_MGR_ACTOR_NAME: &str = "DomainSchedulerManager";
    pub const LOCAL_SCHED_MGR_ACTOR_NAME: &str = "LocalSchedulerManager";

    pub const DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX: &str = "-DomainSchedulerSrv";
    pub const DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX: &str = "-UnderlayerSchedMgr";
    pub const DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME: &str = "UnderlayerSchedMgr";
    pub const DOMAIN_GROUP_CTRL_ACTOR_NAME: &str = "DomainGroupCtrlActor";

    pub const FUNCTION_AGENT_AGENT_SERVICE_ACTOR_NAME: &str = "AgentServiceActor";
    pub const FUNCTION_AGENT_AGENT_MGR_ACTOR_NAME: &str = "AgentMgrActor";

    pub const RUNTIME_MANAGER_ACTOR_NAME: &str = "RuntimeManagerActor";
    pub const RUNTIME_MANAGER_SRV_ACTOR_NAME: &str = "-RuntimeManagerSrv";
    pub const RUNTIME_MANAGER_HEALTH_CHECK_ACTOR_NAME: &str = "HealthCheckActor";
    pub const RUNTIME_MANAGER_LOG_MANAGER_ACTOR_NAME: &str = "LogManagerActor";
    pub const RUNTIME_MANAGER_DEBUG_SERVER_MGR_ACTOR_NAME: &str = "DebugServerMgrActor";
    pub const RUNTIME_MANAGER_VIRTUAL_ENV_MGR_ACTOR_NAME: &str = "VirtualEnvMgrActor";
    pub const FUNCTION_AGENT_VIRTUAL_ENV_MGR_ACTOR_NAME: &str = "AgentVirtualEnvMgrActor";

    pub const FUNCTION_ACCESSOR_HTTP_SERVER: &str = "FunctionAccessorHttpServer";
    pub const FUNCTION_ACCESSOR_CONTROL_ACTOR: &str = "FunctionAccessorControlActor";
    pub const FUNCTION_ACCESSOR_INVOKE_ACTOR: &str = "FunctionAccessorInvokeActor";
    pub const FUNCTION_ACCESSOR_SCHEDULE_ACTOR: &str = "FunctionAccessorScheduleActor";

    pub const SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR: &str = "SystemFunctionLoaderBootstrapActor";

    pub const SCALER_ACTOR: &str = "ScalerActor";
    pub const TRACE_ACTOR: &str = "TraceActor";

    pub const INSTANCE_MANAGER_ACTOR_NAME: &str = "InstanceManagerActor";
    pub const GROUP_MANAGER_ACTOR_NAME: &str = "GroupManagerActor";
    pub const TOKEN_MANAGER_ACTOR_NAME: &str = "TokenManagerActor";
    pub const AKSK_MANAGER_ACTOR_NAME: &str = "AKSKManagerActor";

    pub const IAM_ACTOR: &str = "IAMActor";
    pub const CLUSTER_DEPLOYER_ACTOR: &str = "ClusterDeployerActor";
    pub const RESOURCE_GROUP_MANAGER: &str = "ResourceGroupManager";

    pub const HEARTBEAT_CLIENT_BASENAME: &str = "HeartbeatClient-";
    pub const HEARTBEAT_OBSERVER_BASENAME: &str = "HeartbeatObserver-";

    pub const COMPONENT_NAME_FUNCTION_PROXY: &str = "function_proxy";
    pub const COMPONENT_NAME_FUNCTION_MASTER: &str = "function_master";
    pub const COMPONENT_NAME_DOMAIN_SCHEDULER: &str = "domain_scheduler";
    pub const COMPONENT_NAME_FUNCTION_AGENT: &str = "function_agent";
    pub const COMPONENT_NAME_IAM_SERVER: &str = "iam_server";
    pub const COMPONENT_NAME_RUNTIME_MANAGER: &str = "runtime_manager";
    pub const COMPONENT_NAME_FUNCTION_ACCESSOR: &str = "function_accessor";
}

/// `common/constants/signal.h`
pub mod signal {
    pub const MIN_SIGNAL_NUM: i32 = 1;
    pub const MAX_SIGNAL_NUM: i32 = 1024;
    pub const MIN_USER_SIGNAL_NUM: i32 = 64;

    pub const SHUT_DOWN_SIGNAL: i32 = 1;
    pub const SHUT_DOWN_SIGNAL_ALL: i32 = 2;
    pub const SHUT_DOWN_SIGNAL_SYNC: i32 = 3;
    pub const SHUT_DOWN_SIGNAL_GROUP: i32 = 4;
    pub const GROUP_EXIT_SIGNAL: i32 = 5;
    pub const FAMILY_EXIT_SIGNAL: i32 = 6;
    pub const APP_STOP_SIGNAL: i32 = 7;
    pub const REMOVE_RESOURCE_GROUP: i32 = 8;
    pub const SUBSCRIBE_SIGNAL: i32 = 9;
    pub const NOTIFY_SIGNAL: i32 = 10;
    pub const UNSUBSCRIBE_SIGNAL: i32 = 11;
    pub const INSTANCE_CHECKPOINT_SIGNAL: i32 = 12;
    pub const INSTANCE_TRANS_SUSPEND_SIGNAL: i32 = 13;
    pub const INSTANCE_SUSPEND_SIGNAL: i32 = 14;
    pub const INSTANCE_RESUME_SIGNAL: i32 = 15;
    pub const GROUP_SUSPEND_SIGNAL: i32 = 16;
    pub const GROUP_RESUME_SIGNAL: i32 = 17;

    pub fn signal_to_string(sig: i32) -> &'static str {
        match sig {
            SHUT_DOWN_SIGNAL => "SHUT_DOWN_SIGNAL",
            SHUT_DOWN_SIGNAL_ALL => "SHUT_DOWN_SIGNAL_ALL",
            SHUT_DOWN_SIGNAL_SYNC => "SHUT_DOWN_SIGNAL_SYNC",
            SHUT_DOWN_SIGNAL_GROUP => "SHUT_DOWN_SIGNAL_GROUP",
            GROUP_EXIT_SIGNAL => "GROUP_EXIT_SIGNAL",
            FAMILY_EXIT_SIGNAL => "FAMILY_EXIT_SIGNAL",
            APP_STOP_SIGNAL => "APP_STOP_SIGNAL",
            REMOVE_RESOURCE_GROUP => "REMOVE_RESOURCE_GROUP",
            SUBSCRIBE_SIGNAL => "SUBSCRIBE_SIGNAL",
            NOTIFY_SIGNAL => "NOTIFY_SIGNAL",
            UNSUBSCRIBE_SIGNAL => "UNSUBSCRIBE_SIGNAL",
            INSTANCE_CHECKPOINT_SIGNAL => "INSTANCE_CHECKPOINT_SIGNAL",
            INSTANCE_TRANS_SUSPEND_SIGNAL => "INSTANCE_TRANS_SUSPEND_SIGNAL",
            INSTANCE_SUSPEND_SIGNAL => "INSTANCE_SUSPEND_SIGNAL",
            INSTANCE_RESUME_SIGNAL => "INSTANCE_RESUME_SIGNAL",
            GROUP_SUSPEND_SIGNAL => "GROUP_SUSPEND_SIGNAL",
            GROUP_RESUME_SIGNAL => "GROUP_RESUME_SIGNAL",
            _ => "UnknownSignal",
        }
    }
}

/// `enum class EXECUTOR_TYPE` from `common/constants/constants.h`
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExecutorType {
    Unknown = -1,
    Runtime = 0,
}

/// Selected frequently-used literals from `common/constants/constants.h`.
pub mod scalars {
    pub const DEFAULT_SYSTEM_TIMEOUT: u32 = 180_000;
    pub const DEFAULT_PULL_RESOURCE_INTERVAL: u64 = 1000;
    pub const DEFAULT_DOMAIN_HEARTBEAT_TIMEOUT: u32 = 6000;
    pub const RECONNECT_BACKOFF_INTERVAL: i32 = 100;
    pub const LITEBUS_THREAD_NUM: i32 = 20;
    pub const FUNCTION_AGENT_ID_PREFIX: &str = "function-agent-";
    pub const DEFAULT_MEMORY_DETECTION_INTERVAL: i32 = 1000;
    pub const DEFAULT_OOM_CONSECUTIVE_DETECTION_COUNT: i32 = 3;
}

/// `common/heartbeat/*.h`
pub mod heartbeat {
    pub const DEFAULT_PING_NUMS: u32 = 12;
    pub const DEFAULT_PING_CYCLE_MS: u32 = 1000;
    pub const DEFAULT_PING_PONG_TIMEOUT_MS: u32 = 10_000;
}

/// `register_helper_actor.cpp` defaults
pub mod register {
    pub const REGISTER_HELPER_SUFFIX: &str = "-RegisterHelper";
    pub const REGISTER_HELPER_ACTOR_NAME: &str = "RegisterHelper";
    pub const DEFAULT_REGISTER_TIMEOUT_MS: u64 = 1000;
    pub const DEFAULT_MAX_PING_TIMES: u32 = 12;
}

/// `memory_optimizer.h` + allocator hints
pub mod memory {
    pub const DEFAULT_MAX_ARENA_NUM: i32 = 20;
    pub const DEFAULT_MEMORY_TRIM_INTERVAL_MS: u64 = 10_000;
    pub const DEFAULT_ACTOR_MAILBOX_CAP: usize = 1024;
}
