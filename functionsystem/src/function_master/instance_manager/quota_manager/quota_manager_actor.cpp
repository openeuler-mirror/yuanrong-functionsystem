/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"

#include <chrono>
#include <utility>

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix/message.pb.h"
#include "common/proto/pb/posix/inner_service.pb.h"
#include "common/resource_view/resource_type.h"

namespace function_master {
using namespace functionsystem::resource_view;

namespace {
std::pair<int64_t, int64_t> ExtractResources(const InstanceInfo &info)
{
    const auto &resMap = info.resources().resources();
    double cpu = 0.0;
    double mem = 0.0;
    if (auto it = resMap.find(CPU_RESOURCE_NAME); it != resMap.end()) {
        cpu = it->second.scalar().value();
    }
    if (auto it = resMap.find(MEMORY_RESOURCE_NAME); it != resMap.end()) {
        mem = it->second.scalar().value();
    }
    return { static_cast<int64_t>(cpu), static_cast<int64_t>(mem) };
}

bool IsSystemTenant(const std::string &tenantID)
{
    return tenantID.empty() || tenantID == "0";
}
}  // namespace

QuotaManagerActor::QuotaManagerActor(QuotaConfig config)
    : ActorBase(std::string(QUOTA_MANAGER_ACTOR_NAME)), config_(std::move(config))
{
}

void QuotaManagerActor::Init()
{
    Receive("OnInstanceRunning", &QuotaManagerActor::OnInstanceRunning);
    Receive("OnInstanceExited", &QuotaManagerActor::OnInstanceExited);
    Receive("ForwardQueryInstancesInfoResponse", &QuotaManagerActor::OnSnapshotResponse);

    RebuildUsageFromSnapshot();
}

void QuotaManagerActor::Finalize()
{
    tenantUsage_.clear();
    instanceArrivalTime_.clear();
}

int64_t QuotaManagerActor::NowMs() const
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void QuotaManagerActor::OnInstanceRunning(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    InstanceInfo insInfo;
    if (!insInfo.ParseFromString(msg)) {
        YRLOG_ERROR("QuotaManagerActor: Failed to parse InstanceRunning message");
        return;
    }

    const std::string tenantID = insInfo.tenantid();
    if (IsSystemTenant(tenantID)) {
        return;
    }

    auto [cpu, mem] = ExtractResources(insInfo);
    const std::string &instanceID = insInfo.instanceid();
    int64_t now = NowMs();

    auto &usage = tenantUsage_[tenantID];
    usage.cpuMillicores += cpu;
    usage.memMb += mem;
    usage.sortedInstances.insert({ now, instanceID });
    usage.instanceResources[instanceID] = { cpu, mem };
    instanceArrivalTime_[instanceID] = now;

    YRLOG_DEBUG("QuotaManagerActor: Instance {} running for tenant {}, cpu={}, mem={}",
                instanceID, tenantID, cpu, mem);

    CheckAndEnforce(tenantID);
}

void QuotaManagerActor::OnInstanceExited(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    InstanceInfo insInfo;
    if (!insInfo.ParseFromString(msg)) {
        YRLOG_ERROR("QuotaManagerActor: Failed to parse InstanceExited message");
        return;
    }

    const std::string tenantID = insInfo.tenantid();
    if (IsSystemTenant(tenantID)) {
        return;
    }

    const std::string &instanceID = insInfo.instanceid();

    auto usageIt = tenantUsage_.find(tenantID);
    if (usageIt == tenantUsage_.end()) {
        return;
    }

    auto &usage = usageIt->second;

    // Only reduce usage if this instance is still tracked (not already evicted by CheckAndEnforce)
    auto resIt = usage.instanceResources.find(instanceID);
    if (resIt != usage.instanceResources.end()) {
        usage.cpuMillicores = std::max(int64_t(0), usage.cpuMillicores - resIt->second.first);
        usage.memMb = std::max(int64_t(0), usage.memMb - resIt->second.second);
        usage.instanceResources.erase(resIt);
    }

    auto arrivalIt = instanceArrivalTime_.find(instanceID);
    if (arrivalIt != instanceArrivalTime_.end()) {
        usage.sortedInstances.erase(arrivalIt->second);
        instanceArrivalTime_.erase(arrivalIt);
    }

    YRLOG_DEBUG("QuotaManagerActor: Instance {} exited for tenant {}", instanceID, tenantID);
}

void QuotaManagerActor::CheckAndEnforce(const std::string &tenantID)
{
    if (!config_.IsEnabled()) {
        return;
    }

    auto quota = config_.GetQuota(tenantID);
    auto &usage = tenantUsage_[tenantID];

    bool overCpu = usage.cpuMillicores > quota.cpuMillicores;
    bool overMem = usage.memMb > quota.memLimitMb;
    if (!overCpu && !overMem) {
        return;
    }

    YRLOG_WARN("QuotaManagerActor: Tenant {} quota exceeded, cpu={}/{}, mem={}/{}",
              tenantID, usage.cpuMillicores, quota.cpuMillicores, usage.memMb, quota.memLimitMb);

    while ((usage.cpuMillicores > quota.cpuMillicores || usage.memMb > quota.memLimitMb)
           && !usage.sortedInstances.empty()) {
        auto it = std::prev(usage.sortedInstances.end());
        const std::string instanceID = it->second;

        // Reduce usage immediately for accurate loop termination
        auto resIt = usage.instanceResources.find(instanceID);
        if (resIt != usage.instanceResources.end()) {
            usage.cpuMillicores = std::max(int64_t(0), usage.cpuMillicores - resIt->second.first);
            usage.memMb = std::max(int64_t(0), usage.memMb - resIt->second.second);
            usage.instanceResources.erase(resIt);
        }

        if (!instanceMgrAID_.Name().empty()) {
            inner_service::ForwardKillRequest killReq;
            killReq.set_instanceid(instanceID);
            killReq.set_requestid("QUOTA_EVICT|tenantID=" + tenantID + "|instanceID=" + instanceID);
            Send(instanceMgrAID_, "ForwardKill", killReq.SerializeAsString());
            YRLOG_INFO("QuotaManagerActor: Evicting instance {} from tenant {} due to quota exceeded",
                       instanceID, tenantID);
        }

        instanceArrivalTime_.erase(instanceID);
        usage.sortedInstances.erase(it);
    }

    if (!domainSchedSrvAID_.Name().empty()) {
        ::messages::TenantQuotaExceeded event;
        event.set_tenantid(tenantID);
        event.set_cooldownms(quota.cooldownMs);
        Send(domainSchedSrvAID_, "TenantQuotaExceeded", event.SerializeAsString());
        YRLOG_INFO("QuotaManagerActor: Sent TenantQuotaExceeded for tenant {}, cooldown {}ms",
                   tenantID, quota.cooldownMs);
    }
}

void QuotaManagerActor::RebuildUsageFromSnapshot()
{
    if (instanceMgrAID_.Name().empty()) {
        YRLOG_WARN("QuotaManagerActor: instanceMgrAID not set, skipping snapshot rebuild");
        return;
    }

    messages::QueryInstancesInfoRequest req;
    req.set_requestid("QUOTA_REBUILD_" + std::to_string(NowMs()));
    Send(instanceMgrAID_, "ForwardQueryInstancesInfo", req.SerializeAsString());
}

void QuotaManagerActor::OnSnapshotResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::QueryInstancesInfoResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("QuotaManagerActor: Failed to parse QueryInstancesInfoResponse");
        return;
    }
    OnSnapshotRebuilt(rsp);
}

void QuotaManagerActor::OnSnapshotRebuilt(const messages::QueryInstancesInfoResponse &rsp)
{
    if (rsp.code() != 0) {
        YRLOG_ERROR("QuotaManagerActor: Snapshot rebuild failed with code {}", rsp.code());
        return;
    }

    tenantUsage_.clear();
    instanceArrivalTime_.clear();
    int64_t now = NowMs();

    for (const auto &insInfo : rsp.instanceinfos()) {
        const std::string tenantID = insInfo.tenantid();
        if (IsSystemTenant(tenantID)) {
            continue;
        }

        auto [cpu, mem] = ExtractResources(insInfo);
        const std::string &instanceID = insInfo.instanceid();

        auto &usage = tenantUsage_[tenantID];
        usage.cpuMillicores += cpu;
        usage.memMb += mem;
        usage.sortedInstances.insert({ now, instanceID });
        usage.instanceResources[instanceID] = { cpu, mem };
        instanceArrivalTime_[instanceID] = now;
    }

    YRLOG_INFO("QuotaManagerActor: Rebuilt usage snapshot, {} tenants tracked", tenantUsage_.size());

    for (auto &[tenantID, usage] : tenantUsage_) {
        CheckAndEnforce(tenantID);
    }
}

}  // namespace function_master
