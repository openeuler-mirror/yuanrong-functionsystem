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

#include "runtime_manager.h"

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"
#include "common/constants/actor_name.h"
#include "logs/logging.h"
#include "proto/pb/message_pb.h"
#include "status/status.h"
#include "executor/runtime_executor.h"
#include "port/port_manager.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {
const uint32_t HALF = 2;
const int32_t MAX_REGISTER_RETRY_TIMES = 30;
RuntimeManager::RuntimeManager(const std::string &name) : ActorBase(name)
{
}

void RuntimeManager::Init()
{
    YRLOG_INFO("init RuntimeManagerActor {}", ActorBase::GetAID().Name());
    ActorBase::Receive("StartInstance", &RuntimeManager::StartInstance);
    ActorBase::Receive("StopInstance", &RuntimeManager::StopInstance);
    ActorBase::Receive("QueryInstanceStatusInfo", &RuntimeManager::QueryInstanceStatusInfo);
    ActorBase::Receive("CleanStatus", &RuntimeManager::CleanStatus);
    ActorBase::Receive("UpdateCred", &RuntimeManager::UpdateCred);
    metricsClient_ = std::make_shared<MetricsClient>();
    healthCheckClient_ = std::make_shared<HealthCheck>();
    auto logManagerActor = std::make_shared<LogManagerActor>(RUNTIME_MANAGER_LOG_MANAGER_ACTOR_NAME, GetAID());
    logManagerClient_ = std::make_shared<LogManager>(logManagerActor);
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    runtimeManagerID_ = uuid.ToString();
}

void RuntimeManager::Finalize()
{
    ASSERT_IF_NULL(logManagerClient_);
    logManagerClient_->StopScanLogs();
    for (const auto &iter : executorMap_) {
        iter.second->Stop();
    }
    ActorBase::Finalize();
    PortManager::GetInstance().Clear();
}

litebus::Future<bool> RuntimeManager::GracefulShutdown()
{
    litebus::Promise<bool> p;
    for (const auto &iter : executorMap_) {
        YRLOG_INFO("runtimeManager graceful shutdown, terminate executor name: {}", iter.second->GetName());
        p.Associate(iter.second->GracefulShutdown());
    }
    if (executorMap_.empty()) {
        p.SetValue(true);
    }
    return p.GetFuture().OnComplete(litebus::Defer(GetAID(), &RuntimeManager::OnExecuterShutdown));
}

void RuntimeManager::OnExecuterShutdown()
{
    YRLOG_INFO("send GracefulShutdownFinish to agent: {}", functionAgentAID_.HashString());
    Send(functionAgentAID_, "GracefulShutdownFinish", "");
}

void RuntimeManager::StartInstance(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    if (msg.empty() || !request->ParseFromString(msg)) {
        YRLOG_ERROR("failed to start instance, message({}) from({}) is invalid.", msg, from.HashString());
        return;
    }
    const auto &instance = request->runtimeinstanceinfo();
    if (!CheckStartInstanceRequest(instance)) {
        return;
    }
    (void)receivedStartingReq_.insert(instance.requestid());
    if (CheckInstanceIsDeployed(from, instance)) {
        return;
    }
    auto executor = FindExecutor(EXECUTOR_TYPE(request->type()));
    if (executor == nullptr) {
        YRLOG_ERROR("{}|{}|the type({}) is not supported to start runtime for instance({}).", instance.traceid(),
                    instance.requestid(), request->type(), instance.instanceid());
        messages::StartInstanceResponse response;
        response.set_requestid(instance.requestid());
        response.set_code(static_cast<int32_t>(RUNTIME_MANAGER_PARAMS_INVALID));
        response.set_message("unknown instance type, cannot start instance");
        litebus::Future<messages::StartInstanceResponse> promise;
        promise.SetValue(response);
        litebus::Async(this->GetAID(), &RuntimeManager::StartInstanceResponse, from,
                       request->runtimeinstanceinfo().instanceid(), promise);
        return;
    }
    YRLOG_INFO("{}|{}|begin to start runtime({}) for instance({}).", instance.traceid(), instance.requestid(),
               instance.runtimeid(), instance.instanceid());
    auto vecs = metricsClient_->GetCardIDs();
    (void)executor->StartInstance(request, vecs)
        .OnComplete(
            litebus::Defer(this->GetAID(), &RuntimeManager::CreateInstanceMetrics, std::placeholders::_1, request))
        .OnComplete(
            litebus::Defer(this->GetAID(), &RuntimeManager::CheckHealthForRuntime, std::placeholders::_1, request))
        .OnComplete(litebus::Defer(this->GetAID(), &RuntimeManager::StartInstanceResponse, from,
                                   request->runtimeinstanceinfo().instanceid(), std::placeholders::_1));
}

bool RuntimeManager::CheckInstanceIsDeployed(const litebus::AID &to, const messages::RuntimeInstanceInfo &instance)
{
    if (auto iter(instanceResponseMap_.find(instance.instanceid())); iter != instanceResponseMap_.end()) {
        auto reqID = iter->second.requestid();
        if (reqID == instance.requestid()) {
            YRLOG_INFO("{}|{}|instance({}) has been deployed", instance.traceid(), instance.requestid(),
                       instance.instanceid());
            auto output(iter->second);
            output.set_code(static_cast<int32_t>(RUNTIME_MANAGER_INSTANCE_HAS_BEEN_DEPLOYED));
            (void)Send(to, "StartInstanceResponse", output.SerializeAsString());
        } else {
            YRLOG_WARN("{}|{}|instance({}) exist, but not the same one({})", instance.traceid(), instance.requestid(),
                       instance.instanceid(), reqID);
            messages::StartInstanceResponse response;
            response.set_requestid(instance.requestid());
            response.set_code(static_cast<int32_t>(RUNTIME_MANAGER_INSTANCE_EXIST));
            response.set_message("instance exist but not the same one");
            (void)Send(to, "StartInstanceResponse", response.SerializeAsString());
        }
        (void)receivedStartingReq_.erase(instance.requestid());
        return true;
    }
    return false;
}

bool RuntimeManager::CheckStartInstanceRequest(const messages::RuntimeInstanceInfo &instance)
{
    if (!connected_) {
        YRLOG_ERROR(
            "{}|{}|runtimeManager registration to functionAgent is not complete, "
            "ignore start instance request, instanceID {}.",
            instance.traceid(), instance.requestid(), instance.instanceid());
        return false;
    }
    if (receivedStartingReq_.find(instance.requestid()) != receivedStartingReq_.end()) {
        YRLOG_WARN("{}|{}|received repeated request ignore it", instance.traceid(), instance.requestid());
        return false;
    }
    return true;
}

void RuntimeManager::InnerOomKillInstance(const litebus::Future<Status> &status, const std::string &instanceID,
                                          const std::string &runtimeID, const std::string &requestID)
{
    if (status.IsError() || status.Get().IsError()) {
        YRLOG_ERROR("{}|{}|status get error, can not inner OOM kill instance, runtimeID: {}", requestID, instanceID,
                    runtimeID);
        return;
    }

    auto request = std::make_shared<messages::StopInstanceRequest>();
    request->set_runtimeid(runtimeID);
    request->set_requestid(requestID);
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    request->set_traceid("trace-OOM-Kill_" + runtimeID + "_" + uuid.ToString());
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::RUNTIME));

    auto executor = FindExecutor(EXECUTOR_TYPE(request->type()));
    YRLOG_INFO("{}|{}|begin to oom kill runtime({}).", request->traceid(), requestID, runtimeID);
    executor->GetExecByRuntimeID(runtimeID)
        .Then([healthCheckClient(healthCheckClient_)](const std::shared_ptr<litebus::Exec> &exePtr) {
            return healthCheckClient->StopHeathCheckByPID(exePtr);
        })
        .Then([executor, request]() { return executor->StopInstance(request, true); })
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::DeleteOomNotifyData, std::placeholders::_1, request))
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::DeleteInstanceMetrics, std::placeholders::_1, request))
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::ReleasePort, std::placeholders::_1, request));
}

void RuntimeManager::DeleteOomNotifyData(const litebus::Future<Status> &status,
                                         const std::shared_ptr<messages::StopInstanceRequest> &request)
{
    if (status.IsError() || status.Get().IsError()) {
        YRLOG_ERROR("{}|{}|can not delete inner OOM notify data, stop instance failed, runtimeID({})",
                    request->traceid(), request->requestid(), request->runtimeid());
        return;
    }
    healthCheckClient_->DeleteOomNotifyData(request->requestid());
}

void RuntimeManager::OomKillInstance(const std::string &instanceID, const std::string &runtimeID,
                                     const std::string &requestID)
{
    YRLOG_DEBUG("{}|received event OOM Kill instanceID({}) runtimeID({})", requestID, instanceID, runtimeID);
    healthCheckClient_->NotifyOomKillInstanceInAdvance(requestID, instanceID, runtimeID)
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::InnerOomKillInstance, std::placeholders::_1, instanceID,
                                   runtimeID, requestID));
}

void RuntimeManager::StopInstance(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    auto request = std::make_shared<messages::StopInstanceRequest>();
    if (msg.empty() || !request->ParseFromString(msg)) {
        YRLOG_ERROR("failed to stop instance, message({}) from({}) is invalid.", msg, from.HashString());
        return;
    }
    if (!connected_) {
        YRLOG_ERROR(
            "{}|{}|runtimeManager registration to functionAgent is not complete, "
            "ignore stop instance request for runtime({}).",
            request->traceid(), request->requestid(), request->runtimeid());
        return;
    }
    auto executor = FindExecutor(EXECUTOR_TYPE(request->type()));
    if (executor == nullptr) {
        YRLOG_ERROR("{}|{}|the type({}) is not supported to stop runtime({})", request->traceid(), request->requestid(),
                    request->type(), request->runtimeid());
        messages::StopInstanceResponse response;
        response.set_requestid(request->requestid());
        response.set_runtimeid(request->runtimeid());
        response.set_code(static_cast<int32_t>(RUNTIME_MANAGER_PARAMS_INVALID));
        response.set_message("unknown instance type, cannot stop instance");
        Send(from, "StopInstanceResponse", response.SerializeAsString());
        return;
    }

    YRLOG_INFO("{}|{}|begin to stop runtime({}).", request->traceid(), request->requestid(), request->runtimeid());
    executor->GetExecByRuntimeID(request->runtimeid())
        .Then([healthCheckClient(healthCheckClient_)](const std::shared_ptr<litebus::Exec> &exePtr) {
            return healthCheckClient->StopHeathCheckByPID(exePtr);
        })
        .Then([executor, request]() { return executor->StopInstance(request, false); })
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::DeleteInstanceMetrics, std::placeholders::_1, request))
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::ReleasePort, std::placeholders::_1, request))
        .OnComplete(
            litebus::Defer(GetAID(), &RuntimeManager::StopInstanceResponse, from, std::placeholders::_1, request));
}

void RuntimeManager::HandlePrestartRuntimeExit(const pid_t pid)
{
    auto executor = FindExecutor(EXECUTOR_TYPE(EXECUTOR_TYPE::RUNTIME));
    if (executor == nullptr) {
        return;
    }
    executor->UpdatePrestartRuntimePromise(pid);
}

void RuntimeManager::SetConfig(const Flags &flags)
{
    functionAgentAID_ = litebus::AID(FUNCTION_AGENT_AGENT_SERVICE_ACTOR_NAME, flags.GetAgentAddress());
    auto executor = FindExecutor(EXECUTOR_TYPE::RUNTIME);
    if (executor != nullptr) {
        executor->SetRuntimeConfig(flags);
    }

    RETURN_IF_NULL(metricsClient_);
    if (flags.GetOomKillEnable()) {
        auto runtimeMemoryExceedLimitCallback = std::bind(&RuntimeManager::OomKillInstance, this, std::placeholders::_1,
                                                          std::placeholders::_2, std::placeholders::_3);
        metricsClient_->SetRuntimeMemoryExceedLimitCallback(runtimeMemoryExceedLimitCallback);
        YRLOG_INFO("set OOM Kill callback to metricsClient");
    }
    metricsClient_->SetConfig(flags);

    RETURN_IF_NULL(healthCheckClient_);
    healthCheckClient_->SetConfig(flags);

    RETURN_IF_NULL(logManagerClient_);
    logManagerClient_->SetConfig(flags);

    auto handlePrestartuntimeExit = [aid(GetAID())](const pid_t pid) {
        litebus::Async(aid, &RuntimeManager::HandlePrestartRuntimeExit, pid);
    };
    healthCheckClient_->RegisterProcessExitCallback(handlePrestartuntimeExit);
    nodeID_ = flags.GetNodeID();
    pingTimeoutMs_ = flags.GetSystemTimeout() / HALF;
}

void RuntimeManager::CollectCpuType()
{
    auto cpuType = GetCpuTypeByProc();
    if (cpuType.empty()) {
        cpuType = GetCpuTypeByCommand();
    }
    cpuType_ = cpuType;
}

std::string RuntimeManager::GetCpuTypeByProc()
{
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (!cpuinfo.is_open()) {
        std::cerr << "Unable to open /proc/cpuinfo" << std::endl;
        YRLOG_WARN("unable to open /proc/cpuinfo");
        return "";
    }
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            std::size_t pos = line.find(':');
            if (pos != std::string::npos && pos + 2 < line.size()) { // +2 to skip the colon and the space
                std::string modelName = line.substr(pos + 2);  // +2 to skip the colon and the space
                YRLOG_INFO("CPU Model Name: {}", modelName);
                return modelName;
            }
        }
    }
    cpuinfo.close();
    return "";
}

// exec lscpu command then parse result
std::string RuntimeManager::GetCpuTypeByCommand()
{
    std::string outputStr = ExecuteCommand("lscpu").output;
    for (auto &it : litebus::strings::Split(outputStr, "\n")) {
        if (it.find("Model name") != std::string::npos) {
            auto target = litebus::strings::Split(it, ":");
            if (target.size() > 1) {
                std::string modelName = litebus::strings::Trim(target[1]);
                YRLOG_INFO("CPU Model Name: {}", modelName);
                return modelName;
            }
        }
    }
    return "";
}

std::string RuntimeManager::GetCpuType() const
{
    return cpuType_;
}

std::shared_ptr<ExecutorProxy> RuntimeManager::FindExecutor(EXECUTOR_TYPE type)
{
    if (auto iter(executorMap_.find(type)); iter != executorMap_.end()) {
        return iter->second;
    }
    if (type == EXECUTOR_TYPE::RUNTIME) {
        YRLOG_DEBUG("not found a executor, create a runtime executor.");
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        const std::string name = "RuntimeExecutor_" + uuid.ToString();
        auto executor = std::make_shared<RuntimeExecutor>(name, functionAgentAID_);
        litebus::Spawn(executor, false);
        auto executorProxy = std::make_shared<RuntimeExecutorProxy>(executor);
        (void)executorMap_.insert(std::make_pair(EXECUTOR_TYPE::RUNTIME, executorProxy));
        return executorProxy;
    }
    return nullptr;
}

void RuntimeManager::StartInstanceResponse(const litebus::AID &from, const std::string &instanceID,
                                           const litebus::Future<messages::StartInstanceResponse> &response)
{
    if (response.IsError()) {
        messages::StartInstanceResponse failResponse;
        failResponse.set_code(static_cast<int32_t>(RUNTIME_MANAGER_START_INSTANCE_FAILED));
        failResponse.set_message("start instance failed");
        Send(from, "StartInstanceResponse", failResponse.SerializeAsString());
        return;  // end
    }

    auto output = response.Get();
    if (output.code() != static_cast<int32_t>(SUCCESS)) {
        YRLOG_ERROR("{}|failed to start runtime, code {}", output.requestid(), output.code());
    } else {
        YRLOG_DEBUG("{}|success to start runtime.", output.requestid());
        (void)instanceResponseMap_.emplace(instanceID, output);
    }
    output.mutable_startruntimeinstanceresponse()->set_cputype(cpuType_);
    (void)receivedStartingReq_.erase(output.requestid());
    (void)Send(from, "StartInstanceResponse", output.SerializeAsString());
}

void RuntimeManager::RegisterToFunctionAgent()
{
    RETURN_IF_NULL(metricsClient_);
    resources::ResourceUnit unit = metricsClient_->GetResourceUnit();

    auto request = std::make_shared<messages::RegisterRuntimeManagerRequest>();
    request->set_name(GetAID().Name());
    request->set_address(GetAID().Url());
    request->set_id(runtimeManagerID_);
    auto resourceUnit = request->mutable_resourceunit();
    resourceUnit->CopyFrom(unit);
    for (auto &ins : resourceUnit->instances()) {
        auto instanceID = ins.first;
        std::string runtimeID;
        if (instanceResponseMap_.find(instanceID) == instanceResponseMap_.end()) {
            YRLOG_WARN("failed to find instance({}) in instance info map", instanceID);
        } else {
            runtimeID = instanceResponseMap_[instanceID].startruntimeinstanceresponse().runtimeid();
            resourceUnit->mutable_instances()->at(instanceID).set_runtimeid(runtimeID);
        }
        if (runtimeID.empty() || instanceInfoMap_.find(runtimeID) == instanceInfoMap_.end()) {
            YRLOG_WARN("failed to find instance({}), runtime({}) in runtime info map", instanceID, runtimeID);
        } else {
            auto storageType = instanceInfoMap_[runtimeID].deploymentconfig().storagetype();
            auto requestID = instanceInfoMap_[runtimeID].requestid();
            resourceUnit->mutable_instances()->at(instanceID).set_storagetype(storageType);
            resourceUnit->mutable_instances()->at(instanceID).set_requestid(requestID);
        }
        YRLOG_DEBUG("add instance({}) to regitster info", instanceID);
    }
    auto executor = FindExecutor(EXECUTOR_TYPE::RUNTIME);
    if (executor == nullptr) {
        YRLOG_ERROR("failed to get runtime executor.");
        return;
    }
    (void)executor->GetRuntimeInstanceInfos().Then(
        litebus::Defer(GetAID(), &RuntimeManager::StartRegister, std::placeholders::_1, request));
}

Status RuntimeManager::StartRegister(const std::map<std::string, messages::RuntimeInstanceInfo> &runtimeInfos,
                                     const std::shared_ptr<messages::RegisterRuntimeManagerRequest> &request)
{
    auto infos = request->mutable_runtimeinstanceinfos();
    for (const auto &info : runtimeInfos) {
        (void)infos->insert({ info.first, info.second });
    }
    auto receivedCallBack = [aid(GetAID())](const std::string &msg) {
        litebus::Async(aid, &RuntimeManager::ReceiveRegistered, msg);
    };
    auto timoutCallBack = [aid(GetAID())]() { litebus::Async(aid, &RuntimeManager::RegisterTimeout); };

    RETURN_STATUS_IF_NULL(registerHelper_, StatusCode::POINTER_IS_NULL, "registerHelper pointer is nullptr");
    registerHelper_->SetRegisteredCallback(receivedCallBack);
    registerHelper_->SetRegisterTimeoutCallback(timoutCallBack);
    registerHelper_->StartRegister(functionAgentAID_.Name(), functionAgentAID_.Url(), request->SerializeAsString(),
                                   MAX_REGISTER_RETRY_TIMES);
    return Status::OK();
}

void RuntimeManager::SendStopInstanceResponse(const litebus::AID &from, const std::string &runtimeID,
                                              const std::shared_ptr<messages::StopInstanceResponse> &response)
{
    if (auto iter(instanceInfoMap_.find(runtimeID)); iter != instanceInfoMap_.end()) {
        (void)instanceResponseMap_.erase(iter->second.instanceid());
        (void)instanceInfoMap_.erase(runtimeID);
    }
    Send(from, "StopInstanceResponse", response->SerializeAsString());
}

void RuntimeManager::OnGetRuntimeStatus(const litebus::AID &from,
                                        const std::shared_ptr<messages::StopInstanceRequest> &request,
                                        const std::shared_ptr<messages::StopInstanceResponse> &response,
                                        const litebus::Future<Status> &instanceStatus)
{
    if (instanceStatus.IsError() || instanceStatus.Get().StatusCode() != StatusCode::SUCCESS) {
        YRLOG_ERROR("{}|{}|failed to stop runtime({}), {}.", request->traceid(), request->requestid(),
                    request->runtimeid(), instanceStatus.Get().RawMessage());
        response->set_code(static_cast<int32_t>(RUNTIME_MANAGER_STOP_INSTANCE_FAILED));
        response->set_message(instanceStatus.Get().RawMessage());
    } else {
        YRLOG_INFO("{}|{}|success to stop runtime({}).", request->traceid(), request->requestid(),
                   request->runtimeid());
        response->set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        response->set_message("stop instance success");
    }
    healthCheckClient_->RemoveRuntimeStatusCache(request->runtimeid());
    SendStopInstanceResponse(from, request->runtimeid(), response);
}

void RuntimeManager::StopInstanceResponse(const litebus::AID &from, const litebus::Future<Status> &status,
                                          const std::shared_ptr<messages::StopInstanceRequest> &request)
{
    auto response = std::make_shared<messages::StopInstanceResponse>();
    response->set_runtimeid(request->runtimeid());
    response->set_requestid(request->requestid());
    response->set_traceid(request->traceid());
    if (status.IsError()) {
        YRLOG_ERROR("{}|{}|failed to stop runtime({}).", request->traceid(), request->requestid(),
                    request->runtimeid());
        response->set_code(static_cast<int32_t>(RUNTIME_MANAGER_STOP_INSTANCE_FAILED));
        response->set_message("stop instance failed");
        SendStopInstanceResponse(from, request->runtimeid(), response);
        return;
    } else if (status.Get().IsError()) {
        YRLOG_ERROR("{}|{}|failed({}) to stop runtime({}).", request->traceid(), request->requestid(),
                    status.Get().StatusCode(), request->runtimeid());
        response->set_code(static_cast<int32_t>(status.Get().StatusCode()));
        response->set_message("stop instance failed");
        SendStopInstanceResponse(from, request->runtimeid(), response);
        return;
    }

    // Note: response after runtime child process exited: indicates that resources ownered by runtime are reclaimed.
    healthCheckClient_->GetRuntimeStatus(request->runtimeid())
        .OnComplete(litebus::Defer(GetAID(), &RuntimeManager::OnGetRuntimeStatus, from, request, response,
                                   std::placeholders::_1));
}

void RuntimeManager::DeleteInstanceMetrics(const litebus::Future<Status> &status,
                                           const std::shared_ptr<messages::StopInstanceRequest> &request)
{
    if (status.IsError()) {
        YRLOG_ERROR("{}|{}|can not delete metrics, stop instance failed, runtimeID({})", request->traceid(),
                    request->requestid(), request->runtimeid());
        return;
    }
    if (status.Get().IsError()) {
        YRLOG_ERROR("{}|{}|can not delete metrics, stop instance failed, runtimeID({})", request->traceid(),
                    request->requestid(), request->runtimeid());
        return;
    }
    std::string runtimeID = request->runtimeid();
    if (instanceInfoMap_.find(runtimeID) != instanceInfoMap_.end()) {
        auto instanceInfo = instanceInfoMap_[runtimeID];
        RETURN_IF_NULL(metricsClient_);
        (void)metricsClient_->DeleteInstanceMetrics(instanceInfo.deploymentconfig().deploydir(),
                                                    instanceInfo.instanceid());
        YRLOG_INFO("{}|{}|runtime manager erase collector, runtimeID({}), instanceID({})", request->traceid(),
                   request->requestid(), request->runtimeid(), instanceInfo.instanceid());
    }
}

void RuntimeManager::CreateInstanceMetrics(const litebus::Future<messages::StartInstanceResponse> &response,
                                           const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    if (response.IsError()) {
        YRLOG_ERROR("{}|{}|failed to start instance, do not add metrics collector, instanceID: {}, runtimeID: {}",
                    request->runtimeinstanceinfo().traceid(), request->runtimeinstanceinfo().requestid(),
                    request->runtimeinstanceinfo().instanceid(), request->runtimeinstanceinfo().runtimeid());
        return;
    }
    if (response.Get().code() != static_cast<int32_t>(SUCCESS)) {
        YRLOG_ERROR("{}|{}|failed to start instance, do not add metrics collector, instanceID: {}, runtimeID: {}",
                    request->runtimeinstanceinfo().traceid(), request->runtimeinstanceinfo().requestid(),
                    request->runtimeinstanceinfo().instanceid(), request->runtimeinstanceinfo().runtimeid());
        return;
    }

    auto instanceInfo = request->runtimeinstanceinfo();
    instanceInfoMap_[response.Get().startruntimeinstanceresponse().runtimeid()] = instanceInfo;
    RETURN_IF_NULL(metricsClient_);
    metricsClient_->CreateInstanceMetrics(response, request);
}

void RuntimeManager::ReleasePort(const litebus::Future<Status> &status,
                                 const std::shared_ptr<messages::StopInstanceRequest> &request)
{
    if (status.IsError()) {
        YRLOG_ERROR("{}|{}|status get error, can not release port, runtimeID: {}", request->traceid(),
                    request->requestid(), request->runtimeid());
        return;
    }
    if (status.Get().IsError()) {
        YRLOG_ERROR("{}|{}|status get error, can not release port, runtimeID: {}", request->traceid(),
                    request->requestid(), request->runtimeid());
        return;
    }

    YRLOG_INFO("{}|{}|release port, runtimeID: {}", request->traceid(), request->requestid(), request->runtimeid());
    (void)PortManager::GetInstance().ReleasePort(request->runtimeid());
}

void RuntimeManager::HeartbeatTimeoutHandler(const litebus::AID &from)
{
    YRLOG_ERROR("heartbeat with FunctionAgent({}) timeout", std::string(from));
    connected_ = false;

    RETURN_IF_NULL(registerHelper_);
    registerHelper_->StopPingPongDriver();

    RETURN_IF_NULL(metricsClient_);
    metricsClient_->StopUpdateResource();
    metricsClient_->StopDiskUsageMonitor();
    RegisterToFunctionAgent();
}

void RuntimeManager::CheckHealthForRuntime(const litebus::Future<messages::StartInstanceResponse> &response,
                                           const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    if (response.IsError()) {
        YRLOG_ERROR("{}|{}|failed to start instance, do not check health for instance({}) runtime({})",
                    request->runtimeinstanceinfo().traceid(), request->runtimeinstanceinfo().requestid(),
                    request->runtimeinstanceinfo().instanceid(), request->runtimeinstanceinfo().runtimeid());
        return;
    }
    if (response.Get().code() != static_cast<int32_t>(SUCCESS)) {
        YRLOG_ERROR("{}|{}|failed to start instance, do not check health for instance({}) runtime({})",
                    request->runtimeinstanceinfo().traceid(), request->runtimeinstanceinfo().requestid(),
                    request->runtimeinstanceinfo().instanceid(), request->runtimeinstanceinfo().runtimeid());
        return;
    }

    auto runtimeID = response.Get().startruntimeinstanceresponse().runtimeid();
    auto pid = static_cast<pid_t>(response.Get().startruntimeinstanceresponse().pid());
    YRLOG_INFO("{}|{}|check health for instance({}) runtime({}) pid({})", request->runtimeinstanceinfo().traceid(),
               request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().instanceid(), runtimeID, pid);
    auto instanceID = request->runtimeinstanceinfo().instanceid();
    RETURN_IF_NULL(healthCheckClient_);
    healthCheckClient_->AddRuntimeRecord(functionAgentAID_, pid, instanceID, runtimeID, nodeID_);
}

void RuntimeManager::SetRegisterHelper(const std::shared_ptr<RegisterHelper> helper)
{
    registerHelper_ = helper;
}

void RuntimeManager::Start()
{
    RegisterToFunctionAgent();
    RETURN_IF_NULL(logManagerClient_);
    logManagerClient_->StartScanLogs();
}

void RuntimeManager::ReceiveRegistered(const std::string &message)
{
    YRLOG_INFO("receive registered message");
    messages::RegisterRuntimeManagerResponse response;
    if (!response.ParseFromString(message)) {
        YRLOG_ERROR("failed to parse Registered message");
        return;
    }

    int code = response.code();
    if (code == static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_INFO("succeed to register to FunctionAgent");
        RETURN_IF_NULL(registerHelper_);
        registerHelper_->SetPingPongDriver(
            pingTimeoutMs_, [aid(GetAID())](const litebus::AID &from, HeartbeatConnection connection) {
                YRLOG_INFO("heartbeat with function agent timeout, connection({})", connection);
                litebus::Async(aid, &RuntimeManager::HeartbeatTimeoutHandler, from);
            });
        RETURN_IF_NULL(metricsClient_);
        metricsClient_->UpdateAgentInfo(functionAgentAID_);
        metricsClient_->UpdateRuntimeManagerInfo(GetAID());
        metricsClient_->StartUpdateResource();
        metricsClient_->StartDiskUsageMonitor();
        metricsClient_->StartRuntimeMemoryLimitMonitor();
        RETURN_IF_NULL(healthCheckClient_);
        healthCheckClient_->UpdateAgentInfo(functionAgentAID_);
        connected_ = true;
        return;
    }
    YRLOG_WARN("{}|runtime manager failed to register to FunctionAgent", runtimeManagerID_);
    // clean status after register failed
    CommitSuicide();
}

void RuntimeManager::RegisterTimeout()
{
    YRLOG_WARN("{}|runtime manager register to FunctionAgent timeout", runtimeManagerID_);
    // clean status after register failed
    CommitSuicide();
}

void RuntimeManager::QueryInstanceStatusInfo(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::QueryInstanceStatusRequest request;
    if (!request.ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse QueryInstanceStatusRequest");
        return;
    }
    if (!connected_) {
        YRLOG_ERROR(
            "{}|runtimeManager registration to functionAgent is not complete, "
            "ignore query instance status info, instanceID {}, runtimeID {}.",
            request.requestid(), request.instanceid(), request.runtimeid());
        return;
    }
    if (instanceInfoMap_.find(request.runtimeid()) == instanceInfoMap_.end()) {
        YRLOG_WARN("{}|received query instanceID({}) runtimeID({}). which is not existed", request.requestid(),
                   request.instanceid(), request.runtimeid());
        messages::InstanceStatusInfo info;
        info.set_instanceid(request.instanceid());
        info.set_status(-1);
        info.set_instancemsg("an unknown error caused the instance exited. instance:" + request.instanceid() +
                             " runtime:" + request.runtimeid() + " which is not found. ");
        info.set_type(static_cast<int32_t>(EXIT_TYPE::NONE_EXIT));
        QueryInstanceStatusInfoResponse(from, request.requestid(), info);
        return;
    }
    YRLOG_INFO("{}|received query instanceID({}) runtimeID({}) status.", request.requestid(), request.instanceid(),
               request.runtimeid());

    RETURN_IF_NULL(healthCheckClient_);
    healthCheckClient_->QueryInstanceStatusInfo(request.instanceid(), request.runtimeid())
        .Then(litebus::Defer(GetAID(), &RuntimeManager::QueryInstanceStatusInfoResponse, from, request.requestid(),
                             std::placeholders::_1));
}

Status RuntimeManager::QueryInstanceStatusInfoResponse(const litebus::AID &from, const std::string &requestID,
                                                       const messages::InstanceStatusInfo &info)
{
    messages::QueryInstanceStatusResponse response;
    *response.mutable_instancestatusinfo() = info;
    response.set_requestid(requestID);
    YRLOG_INFO("{}|response query instanceID({}) status.", requestID, info.instanceid());
    (void)Send(from, "QueryInstanceStatusInfoResponse", response.SerializeAsString());
    return Status::OK();
}

void RuntimeManager::CleanStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::CleanStatusRequest cleanStatusRequest;
    if (!cleanStatusRequest.ParseFromString(msg)) {
        YRLOG_ERROR("{}|failed to parse function-agent({}) CleanStatus message", runtimeManagerID_, from.HashString());
        return;
    }

    messages::CleanStatusResponse cleanStatusResponse;
    (void)Send(from, "CleanStatusResponse", cleanStatusResponse.SerializeAsString());

    if (cleanStatusRequest.name() == runtimeManagerID_) {
        YRLOG_WARN("{}|receive CleanStatus from function-agent, runtime-manager gonna to suicide", runtimeManagerID_);
        CommitSuicide();
        return;
    }
    YRLOG_INFO("{}|receive CleanStatus from function-agent, RuntimeManagerID error, err id = {}", runtimeManagerID_,
               cleanStatusRequest.name());
}
void RuntimeManager::CommitSuicide() const
{
    if (!isUnitTestSituation_) {
        (void)raise(SIGINT);
    }
}

void RuntimeManager::UpdateCred(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto request = std::make_shared<messages::UpdateCredRequest>();
    if (!request->ParseFromString(msg)) {
        YRLOG_ERROR("{}|failed to parse function-agent({}) UpdateCred message", runtimeManagerID_, from.HashString());
        return;
    }
    auto requestID = request->requestid();
    auto runtimeID = request->runtimeid();

    YRLOG_DEBUG("{}|{}|runtime-manager({}) receive UpdateCred from function-agent", requestID, runtimeID,
                runtimeManagerID_);

    auto executor = FindExecutor(EXECUTOR_TYPE::RUNTIME);
    if (executor == nullptr) {
        YRLOG_ERROR("{}|{}|failed to get runtime executor", requestID, runtimeID);
        messages::UpdateCredResponse updateTokenResponse;
        updateTokenResponse.set_code(static_cast<int32_t>(RUNTIME_MANAGER_PARAMS_INVALID));
        updateTokenResponse.set_message("failed to get runtime executor");
        Send(from, "UpdateCredResponse", updateTokenResponse.SerializeAsString());
        return;
    }
    executor->UpdateCredForRuntime(request).OnComplete(
        litebus::Defer(this->GetAID(), &RuntimeManager::UpdateCredResponse, from, std::placeholders::_1));
}

void RuntimeManager::UpdateCredResponse(const litebus::AID &to,
                                        const litebus::Future<messages::UpdateCredResponse> &response)
{
    if (response.IsError()) {
        messages::UpdateCredResponse failResponse;
        failResponse.set_code(static_cast<int32_t>(RUNTIME_MANAGER_UPDATE_TOKEN_FAILED));
        failResponse.set_message("update token failed");
        Send(to, "UpdateCredResponse", failResponse.SerializeAsString());
        return;
    }
    const auto &output = response.Get();
    (void)Send(to, "UpdateCredResponse", output.SerializeAsString());
}

litebus::Future<Status> RuntimeManager::NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                            const int limit)
{
    auto executor = FindExecutor(EXECUTOR_TYPE::RUNTIME);
    if (executor == nullptr) {
        YRLOG_ERROR("failed to get runtime executor");
        return Status(StatusCode::FAILED);
    }
    return executor->NotifyInstancesDiskUsageExceedLimit(description, limit);
}

litebus::Future<bool> RuntimeManager::IsRuntimeActive(const std::string &runtimeID)
{
    auto executor = FindExecutor(EXECUTOR_TYPE::RUNTIME);
    if (executor == nullptr) {
        YRLOG_ERROR("failed to get runtime({}) executor", runtimeID);
        return false;
    }
    return executor->IsRuntimeActive(runtimeID);
}
}  // namespace functionsystem::runtime_manager