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

QuotaManagerActor::QuotaManagerActor(QuotaConfig config) : config_(std::move(config)) {}

void QuotaManagerActor::Init()
{
    Receive("OnInstanceRunning", &QuotaManagerActor::OnInstanceRunning);
    Receive("OnInstanceExited", &QuotaManagerActor::OnInstanceExited);

    instanceMgrAID_ = litebus::GetActor(litebus::AID("InstanceManagerActor", ""));
    domainSchedSrvAID_ = litebus::GetActor(litebus::AID("DomainSchedSrvActor", ""));

    if (instanceMgrAID_ == nullptr) {
        YRLOG_WARN("QuotaManagerActor: InstanceManagerActor not found, will retry on next call");
    }
    if (domainSchedSrvAID_ == nullptr) {
        YRLOG_WARN("QuotaManagerActor: DomainSchedSrvActor not found, will retry on next call");
    }

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
    messages::InstanceInfo insInfo;
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
    instanceArrivalTime_[instanceID] = now;

    YRLOG_DEBUG("QuotaManagerActor: Instance {} running for tenant {}, cpu={}, mem={}",
                instanceID, tenantID, cpu, mem);

    CheckAndEnforce(tenantID);
}

void QuotaManagerActor::OnInstanceExited(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::InstanceInfo insInfo;
    if (!insInfo.ParseFromString(msg)) {
        YRLOG_ERROR("QuotaManagerActor: Failed to parse InstanceExited message");
        return;
    }

    const std::string tenantID = insInfo.tenantid();
    if (IsSystemTenant(tenantID)) {
        return;
    }

    auto [cpu, mem] = ExtractResources(insInfo);
    const std::string &instanceID = insInfo.instanceid();

    auto usageIt = tenantUsage_.find(tenantID);
    if (usageIt == tenantUsage_.end()) {
        return;
    }

    auto &usage = usageIt->second;
    usage.cpuMillicores = std::max(int64_t(0), usage.cpuMillicores - cpu);
    usage.memMb = std::max(int64_t(0), usage.memMb - mem);

    auto arrivalIt = instanceArrivalTime_.find(instanceID);
    if (arrivalIt != instanceArrivalTime_.end()) {
        usage.sortedInstances.erase(arrivalIt->second);
        instanceArrivalTime_.erase(arrivalIt);
    }

    YRLOG_DEBUG("QuotaManagerActor: Instance {} exited for tenant {}, cpu={}, mem={}",
                instanceID, tenantID, cpu, mem);
}

void QuotaManagerActor::CheckAndEnforce(const std::string &tenantID)
{
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

        if (instanceMgrAID_ != nullptr) {
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

    if (domainSchedSrvAID_ != nullptr) {
        nlohmann::json event;
        event["tenantID"] = tenantID;
        event["cooldownMs"] = quota.cooldownMs;
        Send(domainSchedSrvAID_, "TenantQuotaExceeded", event.dump());
        YRLOG_INFO("QuotaManagerActor: Sent TenantQuotaExceeded for tenant {}, cooldown {}ms",
                   tenantID, quota.cooldownMs);
    }
}

void QuotaManagerActor::RebuildUsageFromSnapshot()
{
    if (instanceMgrAID_ == nullptr) {
        instanceMgrAID_ = litebus::GetActor(litebus::AID("InstanceManagerActor", ""));
        if (instanceMgrAID_ == nullptr) {
            YRLOG_WARN("QuotaManagerActor: InstanceManagerActor not available for snapshot rebuild");
            return;
        }
    }

    auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
    req->set_requestid("QUOTA_REBUILD_" + std::to_string(NowMs()));

    auto future = Send(instanceMgrAID_, "ForwardQueryInstancesInfo", req->SerializeAsString());

    auto self = shared_from_this();
    future.OnComplete([self](const litebus::Future<std::string> &result) {
        if (!result.IsReady()) {
            YRLOG_ERROR("QuotaManagerActor: QueryInstancesInfo future not ready");
            return;
        }
        auto rsp = std::make_shared<messages::QueryInstancesInfoResponse>();
        if (!rsp->ParseFromString(result.Get())) {
            YRLOG_ERROR("QuotaManagerActor: Failed to parse QueryInstancesInfoResponse");
            return;
        }
        self->OnSnapshotRebuilt(*rsp);
    });
}

void QuotaManagerActor::OnSnapshotRebuilt(const litebus::Future<messages::QueryInstancesInfoResponse> &rsp)
{
    if (!rsp.IsReady()) {
        YRLOG_ERROR("QuotaManagerActor: Snapshot rebuild failed - response not ready");
        return;
    }

    const auto &response = rsp.Get();
    if (response.code() != 0) {
        YRLOG_ERROR("QuotaManagerActor: Snapshot rebuild failed with code {}", response.code());
        return;
    }

    tenantUsage_.clear();
    instanceArrivalTime_.clear();
    int64_t now = NowMs();

    for (const auto &insInfo : response.instanceinfos()) {
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
        instanceArrivalTime_[instanceID] = now;
    }

    YRLOG_INFO("QuotaManagerActor: Rebuilt usage snapshot, {} tenants tracked", tenantUsage_.size());

    for (auto &[tenantID, usage] : tenantUsage_) {
        CheckAndEnforce(tenantID);
    }
}

}  // namespace function_master
