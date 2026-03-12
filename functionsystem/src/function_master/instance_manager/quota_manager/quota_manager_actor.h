/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_ACTOR_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_ACTOR_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include "actor/actor.hpp"
#include "async/future.hpp"

#include "common/proto/pb/posix/message.pb.h"
#include "function_master/instance_manager/quota_manager/quota_config.h"

namespace function_master {

constexpr std::string_view QUOTA_MANAGER_ACTOR_NAME = "QuotaManagerActor";

struct TenantUsage {
    int64_t cpuMillicores{ 0 };
    int64_t memMb{ 0 };
    std::multimap<int64_t, std::string> sortedInstances; // {arrivalTimeMs, instanceID}
};

class QuotaManagerActor : public litebus::ActorBase,
                          public std::enable_shared_from_this<QuotaManagerActor> {
public:
    explicit QuotaManagerActor(QuotaConfig config);
    ~QuotaManagerActor() override = default;

protected:
    void Init() override;
    void Finalize() override;

    void OnInstanceRunning(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnInstanceExited(const litebus::AID &from, std::string &&name, std::string &&msg);

private:
    void CheckAndEnforce(const std::string &tenantID);
    void RebuildUsageFromSnapshot();
    void OnSnapshotRebuilt(const messages::QueryInstancesInfoResponse &rsp);

    int64_t NowMs() const;

    QuotaConfig                                    config_;
    std::unordered_map<std::string, TenantUsage>  tenantUsage_;
    std::unordered_map<std::string, int64_t>      instanceArrivalTime_;

    litebus::AID instanceMgrAID_;
    litebus::AID domainSchedSrvAID_;
};

}  // namespace function_master

#endif
