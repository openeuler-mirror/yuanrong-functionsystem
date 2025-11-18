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

#include "instance_context.h"

#include <csignal>
#include <utils/string_utils.hpp>
#include "common/logs/logging.h"
#include "common/status/status.h"
namespace functionsystem {

static std::set<int32_t> g_nonFatalCode = { SIGHUP, SIGKILL };

InstanceContext::InstanceContext(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq)
    : scheduleRequest_(scheduleReq)
{
    cancelTag_ = std::make_shared<litebus::Promise<std::string>>();
}

const resources::InstanceInfo &InstanceContext::GetInstanceInfo() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance();
}

void InstanceContext::SetInstanceState(InstanceState state, int32_t errCode, int32_t exitCode, const std::string &msg,
                                       const int32_t type)
{
    ASSERT_IF_NULL(scheduleRequest_);
    YRLOG_DEBUG("set instance({}), state({}), exitCode({}), msg({}), type({})",
                scheduleRequest_->instance().instanceid(), fmt::underlying(state), exitCode, msg, type);
    scheduleRequest_->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(state));
    scheduleRequest_->mutable_instance()->mutable_instancestatus()->set_exitcode(exitCode);
    scheduleRequest_->mutable_instance()->mutable_instancestatus()->set_errcode(errCode);
    scheduleRequest_->mutable_instance()->mutable_instancestatus()->set_msg(msg);
    scheduleRequest_->mutable_instance()->mutable_instancestatus()->set_type(type);
}

InstanceState InstanceContext::GetState() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    YRLOG_DEBUG("get instance({}) state({})", scheduleRequest_->instance().instanceid(),
                scheduleRequest_->instance().instancestatus().code());
    return static_cast<InstanceState>(scheduleRequest_->instance().instancestatus().code());
}

std::shared_ptr<messages::ScheduleRequest> InstanceContext::GetScheduleRequestCopy() const
{
    return std::make_shared<messages::ScheduleRequest>(*scheduleRequest_);
}

std::shared_ptr<messages::ScheduleRequest> InstanceContext::GetScheduleRequest() const
{
    return scheduleRequest_;
}

void InstanceContext::UpdateInstanceInfo(const resources::InstanceInfo &instanceInfo)
{
    ASSERT_IF_NULL(scheduleRequest_);
    YRLOG_DEBUG("update instance({}) info, state({})", instanceInfo.instanceid(), instanceInfo.instancestatus().code());
    scheduleRequest_->mutable_instance()->CopyFrom(instanceInfo);
}

void InstanceContext::UpdateOwner(const std::string &owner)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_functionproxyid(owner);
}

std::string InstanceContext::GetOwner() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().functionproxyid();
}

std::string InstanceContext::GetRequestID() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().requestid();
}

bool IsFatal(const int32_t &exitCode)
{
    return g_nonFatalCode.find(exitCode) == g_nonFatalCode.end();
}

void InstanceContext::SetScheduleTimes(const int32_t &scheduleTimes) const
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_scheduletimes(scheduleTimes);
}

void InstanceContext::SetDeployTimes(const int32_t &deployTimes) const
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_deploytimes(deployTimes);
}

int32_t InstanceContext::GetScheduleTimes() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().scheduletimes();
}

int32_t InstanceContext::GetDeployTimes() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().deploytimes();
}

void InstanceContext::SetFunctionAgentIDAndHeteroConfig(const ScheduleResult &result)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_functionagentid(result.id);
    // only set once
    scheduleRequest_->mutable_instance()->clear_schedulerchain();
    (*scheduleRequest_->mutable_instance()->mutable_schedulerchain()->Add()) = result.id;

    // set hetero device IDs
    auto deviceIDs = result.realIDs;
    if (deviceIDs.empty() || deviceIDs[0] == -1) {
        return;
    }

    auto required = scheduleRequest_->instance().resources().resources();
    for (const auto &res : required) {
        auto resourceNameFields = litebus::strings::Split(res.first, "/");
        if (resourceNameFields.size() != HETERO_RESOURCE_FIELD_NUM) {
            continue;
        }
        std::string vendor = resourceNameFields[VENDOR_IDX];
        auto createOpt = scheduleRequest_->mutable_instance()->mutable_createoptions();
        std::string deviceIDsStr;
        for (auto deviceID : deviceIDs) {
            deviceIDsStr += (std::to_string(deviceID) + ",");
        }
        (void)deviceIDsStr.erase(deviceIDsStr.length() - 1);
        (*createOpt)["func-" + vendor + "-DEVICE-IDS"] = deviceIDsStr;
        YRLOG_INFO("{}|{}: {} will be allocated to instance: {}", vendor, scheduleRequest_->requestid(), deviceIDsStr,
                   scheduleRequest_->instance().instanceid());
    }

    // add hetero schedule result to instance info (for instance recover)
    auto *resources = scheduleRequest_->mutable_instance()->mutable_resources()->mutable_resources();
    for (const auto &allocated : result.allocatedVectors) {
        auto *vectors = (*resources)[allocated.first].mutable_vectors();
        (*resources)[allocated.first].set_name(allocated.first);
        (*resources)[allocated.first].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : allocated.second.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
}

void InstanceContext::SetRuntimeID(const std::string &runtimeID)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_runtimeid(runtimeID);
}

void InstanceContext::SetStartTime(const std::string &timeInfo)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_starttime(timeInfo);
}

void InstanceContext::SetRuntimeAddress(const std::string &address)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_runtimeaddress(address);
}

void InstanceContext::IncreaseScheduleRound()
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->set_scheduleround(scheduleRequest_->scheduleround() + 1);
}

uint32_t InstanceContext::GetScheduleRound()
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->scheduleround();
}

void InstanceContext::SetCheckpointed(const bool flag)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_ischeckpointed(flag);
}

void InstanceContext::SetVersion(const int64_t version)
{
    ASSERT_IF_NULL(scheduleRequest_);
    if (version != 0 && version <= scheduleRequest_->instance().version()) {
        YRLOG_DEBUG("{}|can not set version, because new version({}) is <= version({}) of instance({})",
                    scheduleRequest_->instance().requestid(), version, scheduleRequest_->instance().version(),
                    scheduleRequest_->instance().instanceid());
        return;
    }
    YRLOG_DEBUG("{}|set version({}) for instance({}), old version is {}", scheduleRequest_->instance().requestid(),
                version, scheduleRequest_->instance().instanceid(), scheduleRequest_->instance().version());
    scheduleRequest_->mutable_instance()->set_version(version);
}

int64_t InstanceContext::GetVersion() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().version();
}

void InstanceContext::SetDataSystemHost(const std::string &ip)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_datasystemhost(ip);
}

int64_t InstanceContext::GetGracefulShutdownTime() const
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->mutable_instance()->gracefulshutdowntime();
}

void InstanceContext::SetGracefulShutdownTime(const int64_t time)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->mutable_instance()->set_gracefulshutdowntime(time);
}

void InstanceContext::SetTraceID(const std::string &traceID)
{
    ASSERT_IF_NULL(scheduleRequest_);
    scheduleRequest_->set_traceid(traceID);
}

void InstanceContext::TagStop()
{
    ASSERT_IF_NULL(scheduleRequest_);
    (*scheduleRequest_->mutable_instance()->mutable_extensions())["stop"] = "true";
}

bool InstanceContext::IsStopped()
{
    ASSERT_IF_NULL(scheduleRequest_);
    return scheduleRequest_->instance().extensions().find("stop") != scheduleRequest_->instance().extensions().end();
}

void InstanceContext::SetModRevision(const int64_t modRevision)
{
    if (modRevision > modRevision_) {
        modRevision_ = modRevision;
    }
}

int64_t InstanceContext::GetModRevision()
{
    return modRevision_;
}

litebus::Future<std::string> InstanceContext::GetCancelFuture()
{
    return cancelTag_->GetFuture();
}

void InstanceContext::SetCancel(const std::string &reason)
{
    cancelTag_->SetValue(reason);
}

}  // namespace functionsystem