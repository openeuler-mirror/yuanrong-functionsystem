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

#include "common_flags.h"
#include "common/utils/param_check.h"

namespace functionsystem {
const int32_t MIN_THREADS = 3;
const int32_t MAX_THREADS = 50;
const uint32_t MIN_SYSTEM_TIMEOUT = 3000;
const uint32_t MAX_SYSTEM_TIMEOUT = 60 * 60 * 1000;
const uint64_t MIN_PULL_INTERVAL = 500;
const uint64_t MAX_PULL_INTERVAL = 60 * 60 * 1000;
const uint64_t MIN_VALUE = 0;
const uint64_t MAX_VALUE = 1024 * 1024 * 1024;
const uint16_t MAX_PRIORITY_VALUE = 65535;
const uint32_t MIN_TOLERATE_META_STORE_FAILED_TIMES = 1;
const uint32_t MAX_TOLERATE_META_STORE_FAILED_TIMES = 1000;
const uint32_t DEFAULT_TOLERATE_META_STORE_FAILED_TIMES = 60;
const uint32_t MIN_META_HEALTH_CHECK_INTERVAL_MS = 100;
const uint32_t MAX_META_HEALTH_CHECK_INTERVAL_MS = 600000;
const uint32_t META_HEALTH_CHECK_INTERVAL_MS = 10000;
const uint32_t MIN_META_HEALTH_CHECK_TIMEOUTS = 100;
const uint32_t MAX_META_HEALTH_CHECK_TIMEOUTS = 600000;
const uint32_t META_HEALTH_CHECK_TIMEOUTS = 20000;
const std::string DEFAULT_ETCD_TLS_PATH = "/home/sn/resource/etcd";

using namespace litebus::flag;
CommonFlags::CommonFlags()
{
    AddFlag(&CommonFlags::litebusThreadNum_, "litebus_thread_num", "set num of litebus's thread", LITEBUS_THREAD_NUM,
            NumCheck(MIN_THREADS, MAX_THREADS));
    AddFlag(&CommonFlags::systemTimeout_, "system_timeout", "set the system timeout including heartbeat timeout, ms",
            DEFAULT_SYSTEM_TIMEOUT, NumCheck(MIN_SYSTEM_TIMEOUT, MAX_SYSTEM_TIMEOUT));
    AddFlag(&CommonFlags::pullResourceInterval_, "pull_resource_interval", "set the interval of pull resource, ms",
            DEFAULT_PULL_RESOURCE_INTERVAL, NumCheck(MIN_PULL_INTERVAL, MAX_PULL_INTERVAL));
    AddFlag(&CommonFlags::observabilityAgentGrpcPort_, "observability_agent_grpc_port",
            "observability agent's grpc port, default is 4317", DEFAULT_OBSERVABILITY_AGENT_GRPC_PORT);
    AddFlag(&CommonFlags::observabilityPrometheusPort_, "observability_prometheus_port",
            "observability prometheus port, default is 9392", DEFAULT_OBSERVABILITY_PROMETHEUS_PORT);
    AddFlag(&CommonFlags::prometheusPushgatewayPort_, "prometheus_pushgateway_port",
            "observability prometheus pushgateway port, default is 9091", DEFAULT_OBSERVABILITY_PUSHGATEWAY_PORT);
    AddFlag(&CommonFlags::prometheusPushgatewayIP_, "prometheus_pushgateway_ip", "prometheus pushgateway ip", "");
    AddFlag(&CommonFlags::sslEnable_, "ssl_enable", "open mutual authentication", false);
    AddFlag(&CommonFlags::sslDowngradeEnable_, "ssl_downgrade_enable", "enable ssl downgrade", false);
    AddFlag(&CommonFlags::sslBasePath_, "ssl_base_path", "for mutual authentication in function system", "/");
    AddFlag(&CommonFlags::sslRootFile_, "ssl_root_file", "CA cert file for ssl-config", "");
    AddFlag(&CommonFlags::sslCertFile_, "ssl_cert_file", "module cert file for ssl-config", "");
    AddFlag(&CommonFlags::sslKeyFile_, "ssl_key_file", "module key file for ssl-config", "");
    AddFlag(&CommonFlags::minInstanceCpuSize_, "min_instance_cpu_size", "min instance cpu size",
            DEFAULT_MIN_INSTANCE_CPU_SIZE, NumCheck(MIN_VALUE, MAX_VALUE));
    AddFlag(&CommonFlags::minInstanceMemorySize_, "min_instance_memory_size", "min instance memory size",
            DEFAULT_MIN_INSTANCE_MEMORY_SIZE, NumCheck(MIN_VALUE, MAX_VALUE));
    AddFlag(&CommonFlags::maxInstanceCpuSize_, "max_instance_cpu_size", "max instance cpu size",
            DEFAULT_MAX_INSTANCE_CPU_SIZE, NumCheck(MIN_VALUE, MAX_VALUE));
    AddFlag(&CommonFlags::maxInstanceMemorySize_, "max_instance_memory_size", "max instance memory size",
            DEFAULT_MAX_INSTANCE_MEMORY_SIZE, NumCheck(MIN_VALUE, MAX_VALUE));
    AddFlag(&CommonFlags::etcdAddress_, "etcd_address", "For MetaStore to persist.", "");
    AddFlag(&CommonFlags::etcdTablePrefix_, "etcd_table_prefix", "etcd table prefix", "");
    AddFlag(&CommonFlags::metaStoreExcludedKeys_, "meta_store_excluded_keys", "keys not stored in meta store",
            "/yr/podpools,/yr/functions,/yr/iam");
    AddFlag(&CommonFlags::maxPriority_, "max_priority", "schedule max priority", 0,
            NumCheck(uint16_t(0), MAX_PRIORITY_VALUE));
    AddFlag(&CommonFlags::enablePreemption_, "enable_preemption",
            "enable schedule preemption while higher priority, only valid while max_priority > 0", false);
    AddFlag(&CommonFlags::aggregatedStrategy_, "aggregated_strategy",
            "req aggregate strategy, eg: no_aggregate, strictly, relaxed", std::string("no_aggregate"),
            WhiteListCheck({ "no_aggregate", "strictly", "relaxed" }));
    AddFlag(&CommonFlags::clusterId_, "cluster_id", "cluster id", "");
    AddFlag(&CommonFlags::systemAuthMode_, "system_auth_mode", "authentication mode between yuanrong components", "");
    AddFlag(&CommonFlags::scheduleRelaxed_, "schedule_relaxed",
            "enable the relaxed scheduling policy. When the relaxed number of available nodes or pods is selected, the "
            "scheduling progress exits without traversing all nodes or pods.(default -1)",
            -1);
    InitMetaHealthyCheckFlag();
    InitMetricsFlag();
    InitTraceFlag();
    InitETCDAuthFlag();
}

void CommonFlags::InitMetaHealthyCheckFlag()
{
  AddFlag(&CommonFlags::maxTolerateMetaStoreFailedTimes_,
          "max_tolerate_metastore_healthcheck_failed_times",
          "maximum number of etcd healthy check failures that can be tolerated",
          DEFAULT_TOLERATE_META_STORE_FAILED_TIMES,
          NumCheck(MIN_TOLERATE_META_STORE_FAILED_TIMES,
                   MAX_TOLERATE_META_STORE_FAILED_TIMES));
  AddFlag(&CommonFlags::metaStoreCheckHealthIntervalMs_,
          "metastore_healthcheck_interval",
          "meta store health check interval, ms", META_HEALTH_CHECK_INTERVAL_MS,
          NumCheck(MIN_META_HEALTH_CHECK_INTERVAL_MS,
                   MAX_META_HEALTH_CHECK_INTERVAL_MS));
  AddFlag(
      &CommonFlags::metaStoreTimeoutMs_, "metastore_healthcheck_timeout",
      "the timeout of etcd healthcheck rpc, ms", META_HEALTH_CHECK_TIMEOUTS,
      NumCheck(MIN_META_HEALTH_CHECK_TIMEOUTS, MAX_META_HEALTH_CHECK_TIMEOUTS));
}

void CommonFlags::InitMetricsFlag()
{
    AddFlag(&CommonFlags::enableMetrics_, "enable_metrics", "enable metrics", false);
    AddFlag(&CommonFlags::metricsSslEnable_, "metrics_ssl_enable", "enable ssl metrics", false);
    AddFlag(&CommonFlags::metricsConfig_, "metrics_config", "set the config json string of metrics", "");
    AddFlag(&CommonFlags::metricsConfigFile_, "metrics_config_file", "set the config file of metrics", "");
}

void CommonFlags::InitTraceFlag()
{
    AddFlag(&CommonFlags::enableTrace_, "enable_trace", "enable trace", false);
    AddFlag(&CommonFlags::traceConfig_, "trace_config", "set the config json string of trace", "");
}

void CommonFlags::InitETCDAuthFlag()
{
    AddFlag(&CommonFlags::etcdAuthType_, "etcd_auth_type", "set the etcd auth type", "Noauth");
    AddFlag(&CommonFlags::etcdSecretName_, "etcd_secret_name", "set the etcd secret name", "");
    AddFlag(&CommonFlags::etcdSslBasePath_, "etcd_ssl_base_path", "set etcd ssl base path", DEFAULT_ETCD_TLS_PATH);
    AddFlag(&CommonFlags::etcdRootCAFile_, "etcd_root_ca_file", "set the etcd client root ca file", "");
    AddFlag(&CommonFlags::etcdCertFile_, "etcd_cert_file", "set the etcd clint cert file", "");
    AddFlag(&CommonFlags::etcdKeyFile_, "etcd_key_file", "set the etcd client key file", "");
    AddFlag(&CommonFlags::etcdTargetNameOverride_, "etcd_target_name_override", "set etcd target name for ssl verify",
            "");
}

CommonFlags::~CommonFlags()
{
}
}  // namespace functionsystem