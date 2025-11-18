/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "flags.h"

#include "common/utils/param_check.h"

namespace functionsystem::functionmaster {
const uint32_t DEFAULT_SYS_FUNC_RETRY_PERIOD = 5000;
const uint32_t MIN_SYS_FUNC_RETRY_PERIOD = 1000;
const uint32_t MAX_SYS_FUNC_RETRY_PERIOD = 60000;
const uint32_t GRACE_PERIOD_SECONDS = 25;  // s
const uint32_t MIN_GRACE_PERIOD_SECONDS = 0;
const uint32_t MAX_GRACE_PERIOD_SECONDS = 86400;
const std::string DEFAULT_DOMAIN_SCHEDULE_PLUGINS = R"(["Default", "ResourceSelector", "Label", "Heterogeneous"])";
const uint32_t KUBE_CLIENT_RETRY_TIMES = 5;
const uint32_t MIN_KUBE_CLIENT_RETRY_TIMES = 1;
const uint32_t MAX_KUBE_CLIENT_RETRY_TIMES = 100;
const uint32_t KUBE_CLIENT_RETRY_CYCLE_MS = 3000;
const uint32_t MIN_KUBE_CLIENT_RETRY_CYCLE_MS = 1000;
const uint32_t MAX_KUBE_CLIENT_RETRY_CYCLE_MS = 10000;
const uint32_t HEALTH_MONITOR_MAX_FAILURE = 5;
const uint32_t HEALTH_MONITOR_RETRY_INTERVAL = 3000;
const uint32_t DEFAULT_META_STORE_MAX_FLUSH_CONCURRENCY = 100;
const uint32_t DEFAULT_META_STORE_MAX_FLUSH_BATCH_SIZE = 50;
const uint32_t MIN_DOMAIN_HEARTBEAT_TIMEOUT = 3000;
const uint32_t MAX_DOMAIN_HEARTBEAT_TIMEOUT = 60 * 60 * 1000;
using namespace litebus::flag;

Flags::Flags()
{
    using namespace litebus::flag;
    AddFlag(&Flags::logConfig_, "log_config", "Json config file used for log initialization.",
            "{\"filepath\":\"/home/yr/"
            "log\",\"level\":\"DEBUG\",\"rolling\":{\"maxsize\":100,\"maxfiles\":1},\"alsologtostderr\":true}");
    AddFlag(&Flags::nodeID_, "node_id", "The host name.");
    AddFlag(&Flags::ip_, "ip", "IP address for listening.", true, FlagCheckWrraper(IsAddressesValid));
    AddFlag(&Flags::d1_, "d1", "Maximum number of local schedulers managed by a domain.");
    AddFlag(&Flags::d2_, "d2", "Maximum number of domain schedulers managed by a higher level domain.");
    AddFlag(&Flags::metaStoreAddress_, "meta_store_address", "For MetaStore service discover.");
    AddFlag(&Flags::basePath_, "k8s_base_path", "For k8s service discovery.", "");
    AddFlag(&Flags::clientCertFile_, "k8s_client_cert_file", "client cert file to access kube-apiserver.", "");
    AddFlag(&Flags::clientKeyFile_, "k8s_client_key_file", "client key file to access kube-apiserver.", "");
    AddFlag(&Flags::isSkipTlsVerify_, "skip_k8s_tls_verify", "skip k8s tls verification or not.", "false");
    AddFlag(&Flags::k8sNamespace_, "k8s_namespace", "k8s cluster namespace", "default");
    AddFlag(&Flags::sysFuncRetryPeriod_, "sys_func_retry_period", "System function loader retry period.",
            DEFAULT_SYS_FUNC_RETRY_PERIOD, NumCheck(MIN_SYS_FUNC_RETRY_PERIOD, MAX_SYS_FUNC_RETRY_PERIOD));
    AddFlag(&Flags::sysFuncCustomArgs_, "sys_func_custom_args", "System function loader custom args.", "");
    AddFlag(&Flags::runtimeRecoverEnable_, "runtime_recover_enable", "enable recover runtime", false);
    AddFlag(&Flags::isScheduleTolerateAbnormal_, "is_schedule_tolerate_abnormal",
            "enable tolerate underlayer scheduler exception while scheduling", true);
    AddFlag(&Flags::decryptAlgorithm_, "decrypt_algorithm", "decrypt algorithm, eg: GCM, CBC, STS",
            std::string("NO_CRYPTO"), WhiteListCheck({ "NO_CRYPTO", "CBC", "GCM", "STS" }));
    AddFlag(&Flags::electionMode_, "election_mode", "function master selection mode, eg: standalone,etcd,txn,k8s",
            std::string("standalone"), WhiteListCheck({ "etcd", "txn", "k8s", "standalone" }));
    AddFlag(&Flags::electLeaseTTL_, "elect_lease_ttl", "lease ttl of function master election", DEFAULT_ELECT_LEASE_TTL,
            NumCheck(MIN_ELECT_LEASE_TTL, MAX_ELECT_LEASE_TTL));
    AddFlag(&Flags::electKeepAliveInterval_, "elect_keep_alive_interval", "interval of elect's lease keep alive",
            DEFAULT_ELECT_KEEP_ALIVE_INTERVAL, NumCheck(MIN_ELECT_KEEP_ALIVE_INTERVAL, MAX_ELECT_KEEP_ALIVE_INTERVAL));
    AddFlag(&Flags::enablePrintResourceView_, "enable_print_resource_view",
            "whether enable print resource view, which will affect performance in big scale", false);
    AddFlag(&Flags::migratePrefix_, "migrate_prefix", "migrate instance resource prefix", "");
    AddFlag(&Flags::taintToleranceList_, "taint_tolerance_list", "tolerate node taint list", "");
    AddFlag(&Flags::workerTaintExcludeLabels_, "worker_taint_exclude_labels", "worker taint exclude node labels", "");
    AddFlag(&Flags::migrateEnable_, "migrate_enable", "migrate enable when node has some taint", false);
    AddFlag(&Flags::evictedTaintKey_, "evicted_taint_key", "node taint key that will trigger instance evicted", "");
    AddFlag(&Flags::localSchedulerPort_, "local_scheduler_port", "node taint key that will trigger instance evicted",
            "");

    AddFlag(&Flags::systemUpgradeWatchEnable_, "system_upgrade_watch_enable", "whether watch system upgrade", false);
    AddFlag(&Flags::azID_, "az_id", "system az id", 0);
    AddFlag(&Flags::systemUpgradeKey_, "system_upgrade_key", "system upgrade watch key", "");
    AddFlag(&Flags::systemUpgradeWatchAddress_, "system_upgrade_address", "system upgrade watch key address", "");

    AddFlag(&Flags::gracePeriodSeconds_, "grace_period_seconds", "graceful period when delete pod",
            GRACE_PERIOD_SECONDS, NumCheck(MIN_GRACE_PERIOD_SECONDS, MAX_GRACE_PERIOD_SECONDS));
    AddFlag(&Flags::schedulePlugins_, "schedule_plugins", "schedule plugins need to be registered",
            DEFAULT_DOMAIN_SCHEDULE_PLUGINS);

    AddFlag(&Flags::kubeClientRetryTimes_, "kube_client_retry_times", "for k8s client retry request times",
            KUBE_CLIENT_RETRY_TIMES, NumCheck(MIN_KUBE_CLIENT_RETRY_TIMES, MAX_KUBE_CLIENT_RETRY_TIMES));
    AddFlag(&Flags::kubeClientRetryCycleMs_, "kube_api_retry_cycle", "for k8s client retry request cycle",
            KUBE_CLIENT_RETRY_CYCLE_MS, NumCheck(MIN_KUBE_CLIENT_RETRY_CYCLE_MS, MAX_KUBE_CLIENT_RETRY_CYCLE_MS));
    AddFlag(&Flags::healthMonitorMaxFailure_, "health_monitor_max_failure",
            "for k8s client health monitor max failed times", HEALTH_MONITOR_MAX_FAILURE,
            NumCheck(MIN_KUBE_CLIENT_RETRY_TIMES, MAX_KUBE_CLIENT_RETRY_TIMES));
    AddFlag(&Flags::healthMonitorRetryInterval_, "health_Monitor_retry_interval",
            "for k8s client health monitor retry request cycle", HEALTH_MONITOR_RETRY_INTERVAL,
            NumCheck(MIN_KUBE_CLIENT_RETRY_CYCLE_MS, MAX_KUBE_CLIENT_RETRY_CYCLE_MS));
    AddFlag(&Flags::selfTaintPrefix_, "self_taint_prefix", "prefix for adding or removing node taint", "");
    AddFlag(&Flags::servicesPath_, "services_path", "service yaml path", "/");
    AddFlag(&Flags::libPath_, "lib_path", "path of yaml tool lib", "/");
    AddFlag(&Flags::functionMetaPath_, "function_meta_path", "local function meta path", LOCAL_FUNCTION_META_PATH);
    AddFlag(&Flags::enableAbnormalDoubleCheck_, "enable_abnormal_double_check",
            "proxy is abnormal when heartbeat lost and lease expired", false);
    AddFlag(&Flags::domainHeartbeatTimeoutMs_, "domain_heartbeat_timeout", "set the domain heartbeat timeout, ms",
            DEFAULT_DOMAIN_HEARTBEAT_TIMEOUT, NumCheck(MIN_DOMAIN_HEARTBEAT_TIMEOUT, MAX_DOMAIN_HEARTBEAT_TIMEOUT));
    InitScalerFlags();
    InitMetaStoreFlags();
}

void Flags::InitScalerFlags()
{
    AddFlag(&Flags::poolConfigPath_, "pool_config_path", "default pool config json path",
            "/home/sn/scaler/config/functionsystem-pools.json");
    AddFlag(&Flags::agentTemplatePath_, "agent_template_path", "agent template json path",
            "/home/sn/scaler/template/function-agent.json");
    AddFlag(&Flags::enableFrontendPool_, "enable_frontend_pool", "enable frontend pool",
            false);
}

void Flags::InitMetaStoreFlags()
{
    AddFlag(&Flags::enableMetaStore_, "enable_meta_store", "for meta store enable", false);
    AddFlag(&Flags::enablePersistence_, "enable_persistence", "persist meta store to etcd", false);
    AddFlag(&Flags::metaStoreMode_, "meta_store_mode", "meta-store mode, eg. local", "local");
    AddFlag(&Flags::enableSyncFuncSysFunc_, "enable_sync_sys_func", "enable sync system function info to etcd", false);
    AddFlag(&Flags::metaStoreMaxFlushConcurrency_, "meta_store_max_flush_concurrency",
            "max flush concurrency for meta store backup", DEFAULT_META_STORE_MAX_FLUSH_CONCURRENCY,
            NumCheck(static_cast<uint32_t>(0), UINT32_MAX));
    AddFlag(&Flags::metaStoreMaxFlushBatchSize_, "meta_store_max_flush_batch_size",
            "max flush batch size for meta store backup", DEFAULT_META_STORE_MAX_FLUSH_BATCH_SIZE,
            NumCheck(static_cast<uint32_t>(0), UINT32_MAX));
}

const std::string &Flags::GetLogConfig() const
{
    return logConfig_;
}

const std::string &Flags::GetNodeID() const
{
    return nodeID_;
}

const std::string &Flags::GetIP() const
{
    return ip_;
}

[[nodiscard]] const std::string &Flags::GetMetaStoreAddress() const
{
    return metaStoreAddress_;
}

[[nodiscard]] const std::string &Flags::GetK8sBasePath() const
{
    return basePath_;
}

[[nodiscard]] const std::string &Flags::GetK8sClientCertFile() const
{
    return clientCertFile_;
}

[[nodiscard]] const std::string &Flags::GetK8sClientKeyFile() const
{
    return clientKeyFile_;
}

[[nodiscard]] bool Flags::GetIsSkipTlsVerify() const
{
    return isSkipTlsVerify_ == "true";
}

const litebus::Option<int> &Flags::GetD1() const
{
    return d1_;
}

const litebus::Option<int> &Flags::GetD2() const
{
    return d2_;
}

uint32_t Flags::GetSysFuncRetryPeriod() const
{
    return sysFuncRetryPeriod_;
}

const std::string &Flags::GetSysFuncCustomArgs() const
{
    return sysFuncCustomArgs_;
}

bool Flags::GetRuntimeRecoverEnable() const
{
    return runtimeRecoverEnable_;
}

bool Flags::GetIsScheduleTolerateAbnormal() const
{
    return isScheduleTolerateAbnormal_;
}

const std::string &Flags::GetDecryptAlgorithm() const
{
    return decryptAlgorithm_;
}

const std::string &Flags::GetK8sNamespace() const
{
    return k8sNamespace_;
}
}  // namespace functionsystem::functionmaster