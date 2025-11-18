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

namespace functionsystem::domain_scheduler {
using namespace litebus::flag;
Flags::Flags()
{
    AddFlag(&Flags::logConfig_, "log_config", "json format string. For log initialization.",
            "{\"filepath\": \"/home/yr/log\",\"level\": \"DEBUG\",\"rolling\": {\"maxsize\": 100, \"maxfiles\": 1},"
            "\"alsologtostderr\":true}");
    AddFlag(&Flags::globalAddress_, "global_dddress", "global service address", true,
            FlagCheckWrraper(IsAddressesValid));
    AddFlag(&Flags::ip_, "ip", "IP address to listen on.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&Flags::domainListenPort_, "domain_listen_port", "For domain server listening.", true,
            FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::nodeID_, "node_id", "vm id");
    AddFlag(&Flags::isScheduleTolerateAbnormal_, "is_schedule_tolerate_abnormal",
            "enable tolerate underlayer scheduler exception while scheduling", false);
    AddFlag(&Flags::metaStoreAddress_, "meta_store_address", "meta store sevice address", true,
            FlagCheckWrraper(IsAddressesValid));
    AddFlag(&Flags::electionMode_, "election_mode", "function master election mode, eg: etcd, txn, k8s, standalone",
            std::string("standalone"), WhiteListCheck({ "etcd", "txn", "k8s", "standalone" }));
    AddFlag(&Flags::enablePrintResourceView_, "enable_print_resource_view",
            "whether enable print resource view, which will affect performance in big scale", false);
    AddFlag(&Flags::k8sNamespace_, "k8s_namespace", "k8s cluster namespace", "default");
    AddFlag(&Flags::basePath_, "k8s_base_path", "For k8s service discovery.", "");
    AddFlag(&Flags::electKeepAliveInterval_, "elect_keep_alive_interval", "interval of elect's lease keep alive",
            DEFAULT_ELECT_KEEP_ALIVE_INTERVAL,
            NumCheck(MIN_ELECT_KEEP_ALIVE_INTERVAL, MAX_ELECT_KEEP_ALIVE_INTERVAL));
    AddFlag(&Flags::maxPriority_, "max_priority", "schedule max priority", 0);
}
}  // namespace functionsystem::domain_scheduler