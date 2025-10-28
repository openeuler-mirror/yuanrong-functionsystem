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

#include "agent_service_actor.h"

#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>
#include <chrono>
#include <memory>

#include "actor_worker.h"
#include "async/future.hpp"
#include "common/constants/actor_name.h"
#include "common/resource_view/resource_tool.h"
#include "common/utils/exec_utils.h"
#include "common/utils/generate_message.h"
#include "function_agent/common/constants.h"
#include "function_agent/common/utils.h"
#include "logs/logging.h"
#include "metrics/metrics_adapter.h"

namespace functionsystem::function_agent {
using messages::RuleType;

static const int32_t GRACE_SHUTDOWN_DELAY = 3;
static const int32_t GRACE_SHUTDOWN_TIMEOUT_MS = 1000;
static const uint32_t DOWNLOAD_CODE_RETRY_TIMES = 5;

messages::DeployInstanceResponse AgentServiceActor::InitDeployInstanceResponse(
    const int32_t code, const std::string &message, const messages::DeployInstanceRequest &source)
{
    messages::DeployInstanceResponse target;
    target.set_instanceid(source.instanceid());
    target.set_requestid(source.requestid());
    target.set_code(code);
    target.set_message(message);
    return target;
}

void AgentServiceActor::InitKillInstanceResponse(messages::KillInstanceResponse *target,
                                                 const messages::KillInstanceRequest &source)
{
    target->set_instanceid(source.instanceid());
    target->set_requestid(source.requestid());
}

void AgentServiceActor::DeployInstance(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto deployInstanceRequest = std::make_shared<messages::DeployInstanceRequest>();
    if (!deployInstanceRequest->ParseFromString(msg)) {
        YRLOG_ERROR("{}|{}|failed to parse request for instance({}) deployment.", deployInstanceRequest->traceid(),
                    deployInstanceRequest->requestid(), deployInstanceRequest->instanceid());
        return;
    }

    const std::string &requestID = deployInstanceRequest->requestid();
    // if functionAgent registration to localScheduler is not complete, refuse request from localScheduler
    if (!isRegisterCompleted_) {
        YRLOG_ERROR(
            "{}|{}|functionAgent registration to localScheduler is not complete, ignore deploy instance({}) request.",
            deployInstanceRequest->traceid(), requestID, deployInstanceRequest->instanceid());
        return;
    }

    // 1.if instance or request id is illegal, don't deploy and response.
    if (requestID.empty() || deployInstanceRequest->instanceid().empty()) {
        YRLOG_ERROR("{}|request or instance's id is illegal.", deployInstanceRequest->traceid());
        auto resp = InitDeployInstanceResponse(static_cast<int32_t>(StatusCode::FUNC_AGENT_REQUEST_ID_ILLEGAL_ERROR),
                                               "request or instance's id is illegal.", *deployInstanceRequest);
        (void)Send(from, "DeployInstanceResponse", resp.SerializeAsString());
        return;
    }

    // 2.if the deployer not found, don't deploy and response.
    if (auto storageType = deployInstanceRequest->funcdeployspec().storagetype();
        deployers_.find(storageType) == deployers_.end()) {
        YRLOG_ERROR("{}|{}|can't find a deployer for storage type({}), instance({}).", deployInstanceRequest->traceid(),
                    requestID, storageType, deployInstanceRequest->instanceid());

        auto resp = InitDeployInstanceResponse(static_cast<int32_t>(StatusCode::FUNC_AGENT_INVALID_DEPLOYER_ERROR),
                                               "can't found a Deployer for storage type#" + storageType,
                                               *deployInstanceRequest);
        (void)Send(from, "DeployInstanceResponse", resp.SerializeAsString());
        return;
    }

    YRLOG_DEBUG("s3Config credentialType: {}", s3Config_.credentialType);
    std::string storageType = deployInstanceRequest->funcdeployspec().storagetype();
    YRLOG_INFO("{}|{}|received a deploy instance({}) request from {}", deployInstanceRequest->traceid(),
               requestID, deployInstanceRequest->instanceid(), std::string(from));
    deployingRequest_[requestID] = { from, deployInstanceRequest };
    gracefulShutdownTime_ = deployInstanceRequest->gracefulshutdowntime() + GRACE_SHUTDOWN_DELAY;
    // 4. deploy code package (including main, layer, and delegate package) and start runtime
    auto parameters = BuildDeployerParameters(deployInstanceRequest);
    DownloadCodeAndStartRuntime(parameters, deployInstanceRequest);
}

void AgentServiceActor::DownloadCodeAndStartRuntime(
    const std::shared_ptr<std::queue<DeployerParameters>> &deployObjects,
    const std::shared_ptr<messages::DeployInstanceRequest> &req)
{
    if (IsDownloadFailed(req)) {
        DeleteCodeReferByDeployInstanceRequest(req);
        return;
    }
    if (deployObjects->empty()) {
        YRLOG_INFO("{}|s3 object is invalid, directly start runtime({}).", req->requestid(), req->instanceid());
        (void)StartRuntime(req);
        return;
    }

    auto deployObject = deployObjects->front();
    deployObjects->pop();

    // working dir don't need to increase code refer
    if (!IsDelegateWorkingDirPath(deployObject)) {
        // every time before download code, code refer should increase
        AddCodeRefer(deployObject.destination, deployObject.request->instanceid(), deployObject.deployer);
    }

    bool isMonopoly = req->scheduleoption().schedpolicyname() == MONOPOLY_SCHEDULE;
    if (auto iter = deployingObjects_.find(deployObject.destination); iter != deployingObjects_.end()) {
        // code package is downloading
        YRLOG_DEBUG("{}|{}|code package({}) is downloading. instanceID({})", req->traceid(), req->requestid(),
                    deployObject.destination, req->instanceid());
        iter->second.GetFuture().OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::GetDownloadCodeResult,
                                                           deployObjects, req, deployObject.destination,
                                                           std::placeholders::_1));
    } else if (deployObject.deployer->IsDeployed(deployObject.destination, isMonopoly)) {
        // code package had been downloaded
        YRLOG_DEBUG("{}|{}|code package({}) had been downloaded. instanceID({})", req->traceid(), req->requestid(),
                    deployObject.destination, req->instanceid());
        DownloadCodeAndStartRuntime(deployObjects, req);
    } else {
        // start to download code package
        YRLOG_DEBUG("{}|{}|code package({}) start to download code package. instanceID({})", req->traceid(),
                    req->requestid(), deployObject.destination, req->instanceid());
        (void)deployingObjects_.emplace(deployObject.destination, litebus::Promise<DeployResult>{});
        litebus::Async(GetAID(), &AgentServiceActor::AsyncDownloadCode, deployObject.request, deployObject.deployer)
            .Then(litebus::Defer(GetAID(), &AgentServiceActor::UpdateDeployedObjectByDestination, req,
                                 deployObject.destination, std::placeholders::_1))
            .OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::DownloadCodeAndStartRuntime, deployObjects, req));
    }
}

void AgentServiceActor::DownloadCode(const std::shared_ptr<messages::DeployRequest> &request,
                                     const std::shared_ptr<Deployer> &deployer,
                                     const std::shared_ptr<litebus::Promise<DeployResult>> &promise,
                                     const uint32_t retryTimes)
{
    YRLOG_INFO("start to download code for {}, retry times {}", request->instanceid(), retryTimes);
    auto downloadPromise = litebus::Promise<DeployResult>();
    auto handler = [request, deployer, downloadPromise]() { downloadPromise.SetValue(deployer->Deploy(request)); };
    auto actor = std::make_shared<ActorWorker>();
    (void)actor->AsyncWork(handler).OnComplete([actor](const litebus::Future<Status> &) { actor->Terminate(); });
    downloadPromise.GetFuture().Then([aid(GetAID()), request, deployer, promise, retryTimes,
                                      retryDownloadInterval(retryDownloadInterval_)](const DeployResult &result) {
        if ((result.status.StatusCode() == StatusCode::FUNC_AGENT_OBS_ERROR_NEED_RETRY ||
             result.status.StatusCode() == StatusCode::FUNC_AGENT_OBS_CONNECTION_ERROR)) {
            if (retryTimes < DOWNLOAD_CODE_RETRY_TIMES) {
                litebus::AsyncAfter(retryDownloadInterval, aid, &AgentServiceActor::DownloadCode, request, deployer,
                                    promise, retryTimes + 1);
                return Status::OK();
            }
            // retry exceeds threshold, obs connection error results in alarm
            metrics::MetricsAdapter::GetInstance().SendS3Alarm();
        }
        promise->SetValue(result);
        return Status::OK();
    });
}

litebus::Future<DeployResult> AgentServiceActor::AsyncDownloadCode(
    const std::shared_ptr<messages::DeployRequest> &request, const std::shared_ptr<Deployer> &deployer)
{
    auto promise = std::make_shared<litebus::Promise<DeployResult>>();
    DownloadCode(request, deployer, promise, 1);
    return promise->GetFuture();
}

bool AgentServiceActor::IsDownloadFailed(const std::shared_ptr<messages::DeployInstanceRequest> &req)
{
    if (failedDownloadRequests_.find(req->requestid()) == failedDownloadRequests_.end()) {
        return false;
    }
    auto from = deployingRequest_[req->requestid()].from;
    auto deployResult = failedDownloadRequests_[req->requestid()];
    auto resp = InitDeployInstanceResponse(static_cast<int32_t>(deployResult.status.StatusCode()),
                                           deployResult.status.GetMessage(), *req);
    (void)Send(from, "DeployInstanceResponse", resp.SerializeAsString());

    deployingRequest_.erase(req->requestid());
    failedDownloadRequests_.erase(req->requestid());
    return true;
}

void AgentServiceActor::GetDownloadCodeResult(const std::shared_ptr<std::queue<DeployerParameters>> &deployObjects,
                                              const std::shared_ptr<messages::DeployInstanceRequest> &req,
                                              const std::string &destination,
                                              const litebus::Future<DeployResult> &result)
{
    // the request failed to download package (notified by other request)
    const auto &deployResult = result.Get();
    if (deployResult.status.IsError()) {
        failedDownloadRequests_[req->requestid()] = deployResult;
        YRLOG_WARN("{}|{}|code package({}) download failed. instanceID({}). ErrCode({}), Msg({})", req->traceid(),
                   req->requestid(), destination, req->instanceid(), deployResult.status.StatusCode(),
                   deployResult.status.GetMessage());
    }

    DownloadCodeAndStartRuntime(deployObjects, req);
}

bool AgentServiceActor::UpdateDeployedObjectByDestination(const std::shared_ptr<messages::DeployInstanceRequest> &req,
                                                          const std::string &destination, const DeployResult &result)
{
    YRLOG_DEBUG("Update deployed object.");
    auto iter = deployingObjects_.find(destination);
    if (iter == deployingObjects_.end()) {
        return true;
    }
    // notify other request
    iter->second.SetValue(result);

    // the request failed to download package
    if (result.status.IsError()) {
        failedDownloadRequests_[req->requestid()] = result;
        YRLOG_WARN("{}|{}|code package({}) download failed. instanceID({}). ErrCode({}), Msg({})", req->traceid(),
                   req->requestid(), destination, req->instanceid(), result.status.StatusCode(),
                   result.status.GetMessage());
    }

    (void)deployingObjects_.erase(destination);
    return true;
}

std::shared_ptr<std::queue<DeployerParameters>> AgentServiceActor::BuildDeployerParameters(
    const std::shared_ptr<messages::DeployInstanceRequest> &req)
{
    std::shared_ptr<std::queue<DeployerParameters>> parameters = std::make_shared<std::queue<DeployerParameters>>();
    // 1. build main package DeployRequest
    std::string storageType = req->funcdeployspec().storagetype();
    // 'copy' storage type generate final deploy path by objectID(src code path)
    if (storageType == COPY_STORAGE_TYPE) {
        req->mutable_funcdeployspec()->set_objectid(req->funcdeployspec().deploydir());
    }

    if (deployers_.find(storageType) == deployers_.end()) {
        YRLOG_ERROR("code package storage type({}) not found", storageType);
        return parameters;
    }
    auto dest = deployers_[storageType]->GetDestination(
        req->funcdeployspec().deploydir(), req->funcdeployspec().bucketid(), req->funcdeployspec().objectid());
    if (!dest.empty()) {
        auto deployRequest = SetDeployRequestConfig(req, nullptr);
        parameters->push(DeployerParameters{ deployers_[storageType], dest, deployRequest });
    }
    std::string s3DeployDir(req->funcdeployspec().deploydir());  // should be s3 deploy dir for delegate.
    if (auto deployDirIterator = req->createoptions().find("S3_DEPLOY_DIR");
        deployDirIterator != req->createoptions().end()) {
        YRLOG_DEBUG("config s3 deploy dir for delegate.");
        s3DeployDir = deployDirIterator->second;
    }
    // 2. build layers DeployRequest
    AddLayer(req);
    for (const auto &layer : req->funcdeployspec().layers()) {
        auto config(SetDeployRequestConfig(req, std::make_shared<messages::Layer>(layer)));
        config->mutable_deploymentconfig()->set_deploydir(s3DeployDir);
        if (req->scheduleoption().schedpolicyname() == MONOPOLY_SCHEDULE) {
            parameters->push(DeployerParameters{ deployers_[S3_STORAGE_TYPE], s3DeployDir, config });
            continue;
        }
        // Currently, local functions cannot depend on the S3 layer.
        std::string layerDir = litebus::os::Join(s3DeployDir, "layer");
        std::string bucketDir = litebus::os::Join(layerDir, layer.bucketid());
        std::string objectFile = litebus::os::Join(bucketDir, layer.objectid());
        parameters->push(DeployerParameters{ deployers_[S3_STORAGE_TYPE], objectFile, config });
    }

    auto bootstrapIter = req->createoptions().find(DELEGATE_BOOTSTRAP);
    if (bootstrapIter != req->createoptions().end()) {
        (void)req->mutable_createoptions()->insert({ ENV_DELEGATE_BOOTSTRAP, bootstrapIter->second });
    }

    // parse download user code
    auto iter = req->createoptions().find(DELEGATE_DOWNLOAD);
    if (iter == req->createoptions().end()) {
        return parameters;
    }

    auto info = ParseDelegateDownloadInfoByStr(iter->second);
    if (info.IsNone()) {
        YRLOG_ERROR("DELEGATE_DOWNLOAD {} can not parse.", iter->second);
        return parameters;
    }

    // 3. build delegate DeployRequest
    auto config = SetDeployRequestConfig(req, nullptr);
    config->mutable_deploymentconfig()->set_deploydir(s3DeployDir);
    config = BuildDeployRequestConfigByLayerInfo(info.Get(), config);
    if (deployers_.find(info.Get().storageType) == deployers_.end()) {
        YRLOG_ERROR("code package storage type({}) not found", info.Get().storageType);
        return parameters;
    }
    if (info.Get().storageType == WORKING_DIR_STORAGE_TYPE) {
        // 'working_dir' storage type generate final deploy path by objectID(src appID = instanceID)
        config->mutable_deploymentconfig()->set_objectid(req->instanceid());
        // pass codePath (src working dir zip file)
        config->mutable_deploymentconfig()->set_bucketid(info.Get().codePath);
    }
    auto destination = deployers_[info.Get().storageType]->GetDestination(config->deploymentconfig().deploydir(),
                                                                          config->deploymentconfig().bucketid(),
                                                                          config->deploymentconfig().objectid());
    // for monopoly(faas function) will deploy to a fix path(/dcache)
    if (info.Get().storageType == S3_STORAGE_TYPE && req->scheduleoption().schedpolicyname() == MONOPOLY_SCHEDULE) {
        destination = config->deploymentconfig().deploydir();
    }
    if (info.Get().storageType == WORKING_DIR_STORAGE_TYPE) {
        if (destination == info.Get().codePath) {
            // delegate working dir
            req->mutable_funcdeployspec()->set_deploydir(destination);
            req->mutable_funcdeployspec()->set_storagetype(WORKING_DIR_STORAGE_TYPE);
        }
        // pass unzipped working dir to runtime_manager
        (void)req->mutable_createoptions()->insert({ UNZIPPED_WORKING_DIR, destination });
        // pass origin config (src working dir zip file)
        (void)req->mutable_createoptions()->insert({ YR_WORKING_DIR, info.Get().codePath });
        // pass is user start process to app(runtime)
        (void)req->mutable_createoptions()->insert(
            { YR_APP_MODE, (IsAppDriver(req->createoptions())) ? "true" : "false" });
    } else {
        (void)req->mutable_createoptions()->insert({ ENV_DELEGATE_DOWNLOAD, destination });
        (void)req->mutable_createoptions()->insert({ ENV_DELEGATE_DOWNLOAD_STORAGE_TYPE, info.Get().storageType });
    }
    parameters->push(DeployerParameters{ deployers_[info.Get().storageType], destination, config });
    return parameters;
}

void AgentServiceActor::UpdateAgentStatusToLocal(int32_t status, const std::string &msg)
{
    messages::UpdateAgentStatusRequest request;
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    request.set_requestid(uuid.ToString());
    request.set_status(status);
    request.set_message(msg);

    (void)Send(localSchedFuncAgentMgrAID_, "UpdateAgentStatus", request.SerializeAsString());

    updateAgentStatusInfos_[uuid.ToString()] =
        litebus::AsyncAfter(UPDATE_AGENT_STATUS_TIMEOUT, GetAID(), &AgentServiceActor::RetryUpdateAgentStatusToLocal,
                            uuid.ToString(), request.SerializeAsString());
}

void AgentServiceActor::RetryUpdateAgentStatusToLocal(const std::string &requestID, const std::string &msg)
{
    auto agentStatusInfosIter = updateAgentStatusInfos_.find(requestID);
    if (agentStatusInfosIter == updateAgentStatusInfos_.end()) {
        YRLOG_ERROR("requestID {} is not in UpdateAgentStatusInfos.", requestID);
        return;
    }

    Send(localSchedFuncAgentMgrAID_, "UpdateAgentStatus", std::string(msg));
    updateAgentStatusInfos_[requestID] = litebus::AsyncAfter(
        UPDATE_AGENT_STATUS_TIMEOUT, GetAID(), &AgentServiceActor::RetryUpdateAgentStatusToLocal, requestID, msg);
}

void AgentServiceActor::UpdateAgentStatusResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::UpdateAgentStatusResponse response;
    if (msg.empty() || !response.ParseFromString(msg)) {
        YRLOG_ERROR("message {} is invalid!", msg);
        return;
    }

    auto agentStatusInfosIter = updateAgentStatusInfos_.find(response.requestid());
    if (agentStatusInfosIter == updateAgentStatusInfos_.end()) {
        YRLOG_ERROR("requestID {} is not in UpdateAgentStatusInfos.", response.requestid());
        return;
    }

    if (!isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore update agent status response.", response.requestid());
        return;
    }

    (void)litebus::TimerTools::Cancel(agentStatusInfosIter->second);
    (void)updateAgentStatusInfos_.erase(agentStatusInfosIter->first);
}

void AgentServiceActor::UpdateRuntimeStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::UpdateRuntimeStatusRequest req;
    if (msg.empty() || !req.ParseFromString(msg)) {
        YRLOG_ERROR("update runtime status failed, message {} is invalid!", msg);
        return;
    }
    YRLOG_INFO("{}|receive update runtime status request from {}, status {}", req.requestid(), std::string(from),
               req.status());

    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore update runtime status request.", req.requestid());
        return;
    }

    UpdateAgentStatusToLocal(req.status(), req.message());

    messages::UpdateRuntimeStatusResponse rsp;
    rsp.set_requestid(req.requestid());
    rsp.set_status(static_cast<int32_t>(StatusCode::SUCCESS));
    rsp.set_message("update runtime status success");
    (void)Send(from, "UpdateRuntimeStatusResponse", rsp.SerializeAsString());
}

void AgentServiceActor::KillInstance(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto killInstanceRequest = std::make_shared<messages::KillInstanceRequest>();
    if (!killInstanceRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for kill instance.");
        return;
    }

    const std::string &requestID = killInstanceRequest->requestid();
    // if functionAgent registration to localScheduler is not complete, refuse request from localScheduler
    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore kill request for instance({}).", requestID,
                    killInstanceRequest->instanceid());
        return;
    }

    // stop instance
    messages::KillInstanceResponse rsp;

    auto deployerIter = deployers_.find(killInstanceRequest->storagetype());
    if (deployerIter == deployers_.end()) {
        InitKillInstanceResponse(&rsp, *killInstanceRequest);
        rsp.set_code(static_cast<int32_t>(StatusCode::FUNC_AGENT_INVALID_STORAGE_TYPE));
        rsp.set_message("invalid function's storage type " + killInstanceRequest->storagetype());
        YRLOG_ERROR("{}|kill request from {} invalid storage type({}) of instance({})",
                    killInstanceRequest->requestid(), std::string(from), killInstanceRequest->storagetype(),
                    killInstanceRequest->instanceid());
        Send(from, "KillInstanceResponse", rsp.SerializeAsString());
        return;
    }

    killingRequest_[requestID] = { from, killInstanceRequest };
    messages::StopInstanceRequest stopInstanceRequest;
    function_agent::SetStopRuntimeInstanceRequest(stopInstanceRequest, killInstanceRequest);
    YRLOG_INFO("{}|received Kill instance({}) request. Send stop runtime({}) request to RuntimeManager({}-{}).",
               killInstanceRequest->requestid(), killInstanceRequest->instanceid(), killInstanceRequest->runtimeid(),
               registerRuntimeMgr_.name, registerRuntimeMgr_.address);
    Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "StopInstance",
         stopInstanceRequest.SerializeAsString());
}

litebus::Future<Status> AgentServiceActor::SetDeployers(const std::string &storageType,
                                                        const std::shared_ptr<Deployer> &deployer)
{
    deployers_[storageType] = deployer;
    return Status::OK();
}

void AgentServiceActor::Init()
{
    ActorBase::Receive("DeployInstance", &AgentServiceActor::DeployInstance);
    ActorBase::Receive("KillInstance", &AgentServiceActor::KillInstance);
    ActorBase::Receive("StartInstanceResponse", &AgentServiceActor::StartInstanceResponse);
    ActorBase::Receive("StopInstanceResponse", &AgentServiceActor::StopInstanceResponse);
    ActorBase::Receive("Registered", &AgentServiceActor::Registered);
    ActorBase::Receive("UpdateResources", &AgentServiceActor::UpdateResources);
    ActorBase::Receive("UpdateRuntimeStatus", &AgentServiceActor::UpdateRuntimeStatus);
    ActorBase::Receive("UpdateInstanceStatus", &AgentServiceActor::UpdateInstanceStatus);
    ActorBase::Receive("UpdateInstanceStatusResponse", &AgentServiceActor::UpdateInstanceStatusResponse);
    ActorBase::Receive("UpdateAgentStatusResponse", &AgentServiceActor::UpdateAgentStatusResponse);
    ActorBase::Receive("QueryInstanceStatusInfo", &AgentServiceActor::QueryInstanceStatusInfo);
    ActorBase::Receive("QueryInstanceStatusInfoResponse", &AgentServiceActor::QueryInstanceStatusInfoResponse);
    ActorBase::Receive("CleanStatus", &AgentServiceActor::CleanStatus);
    ActorBase::Receive("CleanStatusResponse", &AgentServiceActor::CleanStatusResponse);
    ActorBase::Receive("UpdateCred", &AgentServiceActor::UpdateCred);
    ActorBase::Receive("UpdateCredResponse", &AgentServiceActor::UpdateCredResponse);
    ActorBase::Receive("GracefulShutdownFinish", &AgentServiceActor::GracefulShutdownFinish);
    ActorBase::Receive("SetNetworkIsolationRequest", &AgentServiceActor::SetNetworkIsolationRequest);
    ActorBase::Receive("QueryDebugInstanceInfos", &AgentServiceActor::QueryDebugInstanceInfos);
    ActorBase::Receive("QueryDebugInstanceInfosResponse", &AgentServiceActor::QueryDebugInstanceInfosResponse);

    litebus::Async(GetAID(), &AgentServiceActor::RemoveCodePackageAsync);
}

void AgentServiceActor::TimeOutEvent(HeartbeatConnection connection)
{
    YRLOG_INFO("heartbeat with local scheduler timeout, connection({})", connection);
    if (monopolyUsed_) {
        if (enableRestartForReuse_) {
            YRLOG_INFO("agent was monopoly used by an instance and enableRestartForReuse is true, agent will restart");
            GracefulShutdown().OnComplete(
                [isUnitTestSituation(isUnitTestSituation_)](const litebus::Future<bool> &status) {
                    if (!isUnitTestSituation) {
                        YR_EXIT("function agent restart for reuse");
                    }
                });
            return;
        }
        YRLOG_WARN(
            "the pod was monopoly used by an instance, and instance already exits. this pod is not allow to deploy by "
            "others. registration should be stop and wait pod terminated");
        return;
    }
    litebus::Async(GetAID(), &AgentServiceActor::RegisterAgent)
        .Then(litebus::Defer(GetAID(), &AgentServiceActor::StartPingPong, std::placeholders::_1));
}

void AgentServiceActor::Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::Registered registered;
    if (!registered.ParseFromString(msg)) {
        YRLOG_WARN("invalid registered msg from {} msg {}", std::string(from), msg);
        return;
    }

    if (registerInfo_.registeredPromise.GetFuture().IsOK()) {
        YRLOG_WARN("already received local scheduler registered msg, errCode: {}, errMsg: {}, from: {}",
                   registered.code(), registered.message(), std::string(from));
        return;
    }
    registerInfo_.registeredPromise.SetValue(registered);
    (void)litebus::TimerTools::Cancel(registerInfo_.reRegisterTimer);

    if (registered.code() != int32_t(StatusCode::SUCCESS)) {
        if (registered.code() == static_cast<int32_t>(StatusCode::LS_AGENT_EVICTED)) {
            YRLOG_WARN("agent has been evicted, will not reconnect to it");
            return;
        }

        YRLOG_ERROR("failed to register to local scheduler, errCode: {}, errMsg: {}, from: {}", registered.code(),
                    registered.message(), std::string(from));
        litebus::Async(GetAID(), &AgentServiceActor::CleanRuntimeManagerStatus, 0);
        (void)sendCleanStatusPromise_.GetFuture().OnComplete(
            litebus::Defer(GetAID(), &AgentServiceActor::CommitSuicide));
        return;
    }

    isRegisterCompleted_ = true;
    YRLOG_INFO("succeed to register to local scheduler. from: {}", std::string(from));
}

litebus::Future<messages::Registered> AgentServiceActor::StartPingPong(const messages::Registered &registered)
{
    YRLOG_INFO("gonna startup PingPongActor, agent service name: {}", agentID_);
    pingPongDriver_ = nullptr;
    auto waitPingTimeout = pingTimeoutMs_ / 2;
    pingPongDriver_ = std::make_shared<PingPongDriver>(agentID_, waitPingTimeout ? waitPingTimeout : PING_TIME_OUT_MS,
                                                       [aid(GetAID())](const litebus::AID &, HeartbeatConnection type) {
                                                           litebus::Async(aid, &AgentServiceActor::TimeOutEvent, type);
                                                       });
    ASSERT_IF_NULL(pingPongDriver_);
    litebus::AID localObserver;
    localObserver.SetName(agentID_ + HEARTBEAT_BASENAME);
    localObserver.SetUrl(localSchedFuncAgentMgrAID_.Url());
    localObserver.SetProtocol(litebus::BUS_UDP);
    pingPongDriver_->CheckFirstPing(localObserver);
    return registered;
}

void AgentServiceActor::StartInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::StartInstanceResponse startInstanceResponse;
    if (!startInstanceResponse.ParseFromString(msg)) {
        YRLOG_ERROR("invalid StartInstanceResponse msg from {} msg {}", std::string(from), msg);
        return;
    }

    auto request = deployingRequest_.find(startInstanceResponse.requestid());
    if (request == deployingRequest_.end()) {
        YRLOG_ERROR("{}|can't return start response, maybe instance has been killed.",
                    startInstanceResponse.requestid());
        return;
    }

    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore start instance response.",
                    startInstanceResponse.requestid());
        return;
    }

    // Repeated deploy should not delete code refer
    if (startInstanceResponse.code() == static_cast<int32_t>(RUNTIME_MANAGER_INSTANCE_HAS_BEEN_DEPLOYED)) {
        YRLOG_INFO("{}|instance({}) has been deployed once", startInstanceResponse.requestid(),
                   request->second.request->instanceid());
        startInstanceResponse.set_code(static_cast<int32_t>(SUCCESS));
    }

    if (startInstanceResponse.code() != 0) {
        YRLOG_ERROR("{}|received start instance response from {}, error code: {}", startInstanceResponse.requestid(),
                    std::string(from), startInstanceResponse.code());
        DeleteCodeReferByDeployInstanceRequest(request->second.request);
    } else {
        YRLOG_INFO("{}|received start instance response. instance({}) runtime({}) address({}) pid({})",
                   startInstanceResponse.requestid(), request->second.request->instanceid(),
                   startInstanceResponse.startruntimeinstanceresponse().runtimeid(),
                   startInstanceResponse.startruntimeinstanceresponse().address(),
                   startInstanceResponse.startruntimeinstanceresponse().pid());
    }

    auto deployInstanceResponse = BuildDeployInstanceResponse(startInstanceResponse, request->second.request);
    (void)runtimesDeploymentCache_->runtimes.emplace(deployInstanceResponse->runtimeid(),
                                                     SetRuntimeInstanceInfo(request->second.request));
    if (auto ret =
            Send(localSchedFuncAgentMgrAID_, "DeployInstanceResponse", deployInstanceResponse->SerializeAsString());
        ret != 1) {
        YRLOG_ERROR("{}|failed({}) to send a response message.", deployInstanceResponse->requestid(), ret);
    }

    (void)deployingRequest_.erase(request);
}

void AgentServiceActor::StopInstanceResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::StopInstanceResponse stopInstanceResponse;
    if (!stopInstanceResponse.ParseFromString(msg)) {
        YRLOG_WARN("invalid StopInstanceResponse msg from {} msg {}", std::string(from), msg);
        return;
    }
    auto requestID = stopInstanceResponse.requestid();
    auto runtimeID = stopInstanceResponse.runtimeid();
    YRLOG_INFO("{}|received StopInstance response from {}, runtimeID: {}", requestID, std::string(from), runtimeID);

    auto request = killingRequest_.find(requestID);
    if (request == killingRequest_.end()) {
        YRLOG_ERROR("Request({}) maybe already killed.", requestID);
        return;
    }

    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore stop instance response.", requestID);
        return;
    }

    auto killInstanceRequest = request->second.request;
    auto killInstanceResponse = BuildKillInstanceResponse(stopInstanceResponse.code(), stopInstanceResponse.message(),
                                                          requestID, killInstanceRequest->instanceid());
    YRLOG_DEBUG("{}|AgentServiceActor send KillInstanceResponse back to {}", requestID,
                std::string(request->second.from));
    Send(request->second.from, "KillInstanceResponse", killInstanceResponse->SerializeAsString());

    // If a pod is exclusively occupied by an instance, the pod cannot be used by other instances after the instance
    // exits.
    if (killInstanceRequest->ismonopoly()) {
        monopolyUsed_ = true;
    }

    // clear function's code package
    auto runtimeIter = runtimesDeploymentCache_->runtimes.find(runtimeID);
    if (runtimeIter == runtimesDeploymentCache_->runtimes.end()) {
        YRLOG_ERROR("AgentServiceActor failed to find deployment config of runtime {}", runtimeID);
        return;
    }

    DeleteCodeReferByRuntimeInstanceInfo(runtimeIter->second);

    (void)runtimesDeploymentCache_->runtimes.erase(runtimeID);
    (void)killingRequest_.erase(requestID);
}

void AgentServiceActor::UpdateResources(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::UpdateResourcesRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_WARN("invalid update resource request msg from {} msg {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("received UpdateResources request from {}", std::string(from));
    if (!registerRuntimeMgr_.registered) {
        YRLOG_ERROR("functionAgent({}) registration is not complete, ignore update resources request.", agentID_);
        return;
    }

    req.mutable_resourceunit()->set_id(agentID_);
    req.mutable_resourceunit()->set_alias(alias_);
    resources::Value::Counter cnter;
    (void)req.mutable_resourceunit()->mutable_nodelabels()->insert({ agentID_, cnter });
    registeredResourceUnit_->CopyFrom(req.resourceunit());
    (void)Send(localSchedFuncAgentMgrAID_, "UpdateResources", req.SerializeAsString());
}

void AgentServiceActor::UpdateInstanceStatus(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("agent({}) registration is not complete, ignore update instance status request.", agentID_);
        return;
    }

    (void)Send(localSchedFuncAgentMgrAID_, "UpdateInstanceStatus", std::move(msg));
}

void AgentServiceActor::UpdateInstanceStatusResponse(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("agent({}) registration is not complete, ignore update instance status response.", agentID_);
        return;
    }

    (void)Send(litebus::AID(RUNTIME_MANAGER_HEALTH_CHECK_ACTOR_NAME, registerRuntimeMgr_.address),
               "UpdateInstanceStatusResponse", std::move(msg));
}

litebus::Future<messages::Registered> AgentServiceActor::RegisterAgent()
{
    YRLOG_INFO("AgentServiceActor start to RegisterAgent to {}", std::string(localSchedFuncAgentMgrAID_));
    messages::Registered response;
    if (registeredResourceUnit_ == nullptr) {
        std::string msg =
            "AgentServiceActor nullptr of registeredResourceUnit_! Maybe runtime_manager is not registered.";
        YRLOG_ERROR(msg);
        response.set_code(static_cast<int32_t>(StatusCode::FUNC_AGENT_RESOURCE_UNIT_IS_NULL));
        response.set_message(msg);
        return response;
    }
    messages::Register registerAgentRequest;
    auto resourceUnit = registerAgentRequest.mutable_resource();
    registeredResourceUnit_->set_id(agentID_);
    registeredResourceUnit_->set_alias(alias_);
    resourceUnit->CopyFrom(*registeredResourceUnit_);

    // Set Registration information
    messages::FuncAgentRegisInfo funcAgentRegisInfo;
    funcAgentRegisInfo.set_agentaidname(std::string(GetAID().Name()));
    funcAgentRegisInfo.set_runtimemgraid(registerRuntimeMgr_.name);
    funcAgentRegisInfo.set_runtimemgrid(registerRuntimeMgr_.id);
    funcAgentRegisInfo.set_agentaddress(GetAID().Url());
    std::string jsonStr;
    auto ret = google::protobuf::util::MessageToJsonString(funcAgentRegisInfo, &jsonStr);
    if (!ret.ok()) {
        std::string msg = "serialize function agent registration information to json format string failed.";
        YRLOG_ERROR(msg);
        response.set_code(static_cast<int32_t>(StatusCode::FUNC_AGENT_REGIS_INFO_SERIALIZED_FAILED));
        response.set_message(msg);
        return response;
    }
    registerAgentRequest.set_message(jsonStr);
    registerAgentRequest.set_name(agentID_);

    registerInfo_.reRegisterTimer =
        litebus::AsyncAfter(retryRegisterInterval_, GetAID(), &AgentServiceActor::RetryRegisterAgent,
                            registerAgentRequest.SerializeAsString());
    registerInfo_.registeredPromise = litebus::Promise<messages::Registered>();

    YRLOG_INFO("AgentServiceActor gonna send Register request to {}", std::string(localSchedFuncAgentMgrAID_));
    Send(localSchedFuncAgentMgrAID_, "Register", registerAgentRequest.SerializeAsString());
    return registerInfo_.registeredPromise.GetFuture();
}

void AgentServiceActor::RetryRegisterAgent(const std::string &msg)
{
    auto registerResponseFuture = registerInfo_.registeredPromise.GetFuture();
    if (registerResponseFuture.IsOK()) {
        return;
    }

    YRLOG_INFO("AgentServiceActor gonna send Register request to {}", std::string(localSchedFuncAgentMgrAID_));
    Send(localSchedFuncAgentMgrAID_, "Register", std::string(msg));
    registerInfo_.reRegisterTimer =
        litebus::AsyncAfter(retryRegisterInterval_, GetAID(), &AgentServiceActor::RetryRegisterAgent, msg);
}

void AgentServiceActor::MarkRuntimeManagerUnavailable(const std::string &id)
{
    registerHelper_->StopHeartbeatObserver();
    if (registerRuntimeMgr_.id != id) {
        YRLOG_ERROR("failed to find RuntimeManager({}) info", id);
        return;
    }

    YRLOG_WARN("gonna mark RuntimeManager {} as unavailable", id);
    runtimeManagerGracefulShutdown_.SetValue(true);
    registerRuntimeMgr_.registered = false;

    UpdateAgentStatusToLocal(static_cast<int32_t>(RUNTIME_MANAGER_REGISTER_FAILED));
}

Status AgentServiceActor::StartRuntime(const DeployInstanceRequest &request)
{
    auto startInstanceRequest = std::make_unique<messages::StartInstanceRequest>();
    function_agent::SetStartRuntimeInstanceRequestConfig(startInstanceRequest, request);
    if (request->funcdeployspec().storagetype() == COPY_STORAGE_TYPE) {
        startInstanceRequest->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir(
            deployers_[COPY_STORAGE_TYPE]->GetDestination("", "", request->funcdeployspec().deploydir()));
    }

    if (!registerRuntimeMgr_.registered) {
        YRLOG_ERROR("{}|{}|runtime-manager not registered, failed to StartRuntime. instance {}", request->traceid(),
                    request->requestid(), request->instanceid());
        auto resp = InitDeployInstanceResponse(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION),
                                               "invalid runtime-manager", *request);
        (void)Send(localSchedFuncAgentMgrAID_, "DeployInstanceResponse", resp.SerializeAsString());
        return Status(StatusCode::FUNC_AGENT_START_RUNTIME_FAILED, "invalid runtime-manager");
    }
    YRLOG_INFO("{}|{}|send StartInstance request to ({}-{}), instance: {}", request->traceid(), request->requestid(),
               registerRuntimeMgr_.name, registerRuntimeMgr_.address, request->instanceid());
    Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "StartInstance",
         startInstanceRequest->SerializeAsString());

    return Status::OK();
}

void AgentServiceActor::SetRegisterHelper(const std::shared_ptr<RegisterHelper> &helper)
{
    registerHelper_ = nullptr;
    registerHelper_ = helper;
    auto func = [aid(GetAID())](const std::string &message) {
        litebus::Async(aid, &AgentServiceActor::ReceiveRegister, message);
    };
    registerHelper_->SetRegisterCallback(func);
}

void AgentServiceActor::ReceiveRegister(const std::string &message)
{
    YRLOG_INFO("receive register message");
    messages::RegisterRuntimeManagerResponse rsp;
    messages::RegisterRuntimeManagerRequest req;
    if (!req.ParseFromString(message)) {
        YRLOG_ERROR("failed to parse RuntimeManager register message");
        return;
    }

    if (registerRuntimeMgr_.id == req.id()) {
        if (registerRuntimeMgr_.registered) {
            YRLOG_INFO(
                "{}|FunctionAgent has received RuntimeManager(id:{}) register request before, discard this request",
                agentID_, req.id());
            rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        } else {
            YRLOG_WARN("{}|FunctionAgent receive RuntimeManager(id:{}) pong timeout and retry register failed",
                       agentID_, req.id());
            rsp.set_code(static_cast<int32_t>(StatusCode::REGISTER_ERROR));
        }
        registerHelper_->SendRegistered(req.name(), req.address(), rsp.SerializeAsString());
        return;
    }

    // update agent service actor's cache
    registerRuntimeMgr_ = { req.name(), req.address(), req.id(), true };
    auto timeoutHandler = [aid(GetAID()), id(registerRuntimeMgr_.id)](const litebus::AID &) {
        litebus::Async(aid, &AgentServiceActor::MarkRuntimeManagerUnavailable, id);
    };
    registerHelper_->SetHeartbeatObserveDriver(registerRuntimeMgr_.name, registerRuntimeMgr_.address, pingTimeoutMs_,
                                               timeoutHandler);

    auto requestInstanceInfos = req.runtimeinstanceinfos();
    for (const auto &it : requestInstanceInfos) {
        (void)runtimesDeploymentCache_->runtimes.emplace(it.first, it.second);
        AddCodeReferByRuntimeInstanceInfo(it.second);
    }
    registeredResourceUnit_->CopyFrom(req.resourceunit());

    // send Registered message back to runtime_manager
    rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    YRLOG_INFO("gonna send Registered message back to RuntimeManager({}-{})", registerRuntimeMgr_.name,
               registerRuntimeMgr_.address);
    registerHelper_->SendRegistered(registerRuntimeMgr_.name, registerRuntimeMgr_.address, rsp.SerializeAsString());

    // start to register function_agent to local_scheduler
    RegisterAgent()
        .Then(litebus::Defer(GetAID(), &AgentServiceActor::StartPingPong, std::placeholders::_1));
}

void AgentServiceActor::AddCodeReferByRuntimeInstanceInfo(const messages::RuntimeInstanceInfo &info)
{
    const std::string &instanceID = info.instanceid();
    // add executor function refer
    auto deployerIter = deployers_.find(info.deploymentconfig().storagetype());
    if (deployerIter == deployers_.end()) {
        YRLOG_ERROR("{}|instance add code refer error, do not have this type of deployer, type = {}", info.instanceid(),
                    info.deploymentconfig().storagetype());
        return;
    }
    auto executorDestination = deployerIter->second->GetDestination(
        info.deploymentconfig().deploydir(), info.deploymentconfig().bucketid(), info.deploymentconfig().objectid());
    AddCodeRefer(executorDestination, instanceID, deployerIter->second);

    auto s3DeployerIter = deployers_.find(S3_STORAGE_TYPE);
    if (s3DeployerIter == deployers_.end()) {
        YRLOG_ERROR("{}|instance add code refer error, do not have S3 deployer", info.instanceid());
        return;
    }
    // add layer function refer
    std::string s3DeployDir(info.deploymentconfig().deploydir());  // should be s3 deploy dir for delegate.
    if (auto deployDirIterator = info.runtimeconfig().userenvs().find("S3_DEPLOY_DIR");
        deployDirIterator != info.runtimeconfig().userenvs().end()) {
        s3DeployDir = deployDirIterator->second;
    }
    for (auto &layer : info.deploymentconfig().layers()) {
        auto layerDestination = s3DeployDir + "/layer/" + layer.bucketid() + "/" + layer.objectid();
        AddCodeRefer(layerDestination, instanceID, deployers_[S3_STORAGE_TYPE]);
    }

    // add delegate user code function refer
    auto userCodeDestinationIter = info.runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD);
    if (userCodeDestinationIter == info.runtimeconfig().posixenvs().end()) {
        return;
    }
    auto delegateCodeStorageIter = info.runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD_STORAGE_TYPE);
    if (delegateCodeStorageIter == info.runtimeconfig().posixenvs().end()) {
        return;
    }
    AddCodeRefer(userCodeDestinationIter->second, instanceID, deployers_[delegateCodeStorageIter->second]);
}

void AgentServiceActor::AddCodeRefer(const std::string &dstDir, const std::string &instanceID,
                                     const std::shared_ptr<Deployer> &deployer)
{
    ASSERT_IF_NULL(codeReferInfos_);
    if (auto iter = codeReferInfos_->find(dstDir); iter == codeReferInfos_->end()) {
        (void)codeReferInfos_->emplace(dstDir, CodeReferInfo{ { instanceID }, deployer });
    } else {
        (void)iter->second.instanceIDs.emplace(instanceID);
    }
}

void AgentServiceActor::DeleteCodeReferByDeployInstanceRequest(
    const std::shared_ptr<messages::DeployInstanceRequest> &req)
{
    const auto &instanceID = req->instanceid();
    // delete executor function refer
    auto executorDestination = deployers_[req->funcdeployspec().storagetype()]->GetDestination(
        req->funcdeployspec().deploydir(), req->funcdeployspec().bucketid(), req->funcdeployspec().objectid());
    DeleteFunction(executorDestination, instanceID);

    // delete layer function refer
    std::string s3DeployDir(req->funcdeployspec().deploydir());  // should be s3 deploy dir for delegate.
    if (auto deployDirIterator = req->createoptions().find("S3_DEPLOY_DIR");
        deployDirIterator != req->createoptions().end()) {
        s3DeployDir = deployDirIterator->second;
    }
    for (auto &layer : req->funcdeployspec().layers()) {
        auto layerDestination = s3DeployDir + "/layer/" + layer.bucketid() + "/" + layer.objectid();
        DeleteFunction(layerDestination, instanceID);
    }

    // delete delegate user code function refer
    auto userCodeDestinationIter = req->createoptions().find(ENV_DELEGATE_DOWNLOAD);
    if (userCodeDestinationIter == req->createoptions().end()) {
        return;
    }
    DeleteFunction(userCodeDestinationIter->second, instanceID);
}

void AgentServiceActor::DeleteCodeReferByRuntimeInstanceInfo(const messages::RuntimeInstanceInfo &info)
{
    const auto &instanceID = info.instanceid();
    // delete executor function refer
    auto executorDestination = deployers_[info.deploymentconfig().storagetype()]->GetDestination(
        info.deploymentconfig().deploydir(), info.deploymentconfig().bucketid(), info.deploymentconfig().objectid());
    DeleteFunction(executorDestination, instanceID);

    // delete layer function refer
    std::string s3DeployDir(info.deploymentconfig().deploydir());  // should be s3 deploy dir for delegate.
    if (auto deployDirIterator = info.runtimeconfig().userenvs().find("S3_DEPLOY_DIR");
        deployDirIterator != info.runtimeconfig().userenvs().end()) {
        s3DeployDir = deployDirIterator->second;
    }
    for (auto &layer : info.deploymentconfig().layers()) {
        auto layerDestination = s3DeployDir + "/layer/" + layer.bucketid() + "/" + layer.objectid();
        DeleteFunction(layerDestination, instanceID);
    }

    // delete delegate user code function refer
    auto userCodeDestinationIter = info.runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD);
    if (userCodeDestinationIter == info.runtimeconfig().posixenvs().end()) {
        return;
    }
    DeleteFunction(userCodeDestinationIter->second, instanceID);
}

void AgentServiceActor::DeleteFunction(const std::string &functionDestination, const std::string &instanceID)
{
    ASSERT_IF_NULL(codeReferInfos_);
    auto iter = codeReferInfos_->find(functionDestination);
    if (iter == codeReferInfos_->end()) {
        return;
    }
    if (iter->second.instanceIDs.find(instanceID) != iter->second.instanceIDs.end()) {
        iter->second.lastAccessTimestamp = static_cast<uint64_t>(std::time(nullptr));
        (void)iter->second.instanceIDs.erase(instanceID);
    }
}

void AgentServiceActor::QueryInstanceStatusInfo(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered) {
        YRLOG_ERROR("{}|registration is not complete, ignore query instance status info.", agentID_);
        return;
    }
    (void)Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "QueryInstanceStatusInfo",
               std::move(msg));
}

void AgentServiceActor::QueryInstanceStatusInfoResponse(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore query instance status response.", agentID_);
        return;
    }
    (void)Send(localSchedFuncAgentMgrAID_, "QueryInstanceStatusInfoResponse", std::move(msg));
}

void AgentServiceActor::RemoveCodePackageAsync()
{
    if (remainedClearCodePackageRetryTimes_ == 0) {
        YRLOG_WARN("{}|agent failed to clean code package when clean status", agentID_);
        clearCodePackagePromise_.SetValue(StatusCode::FUNC_AGENT_CLEAN_CODE_PACKAGE_TIME_OUT);
        return;
    }

    ASSERT_IF_NULL(codeReferInfos_);
    if (isCleaningStatus_ && codeReferInfos_->empty()) {
        YRLOG_INFO("{}|agent success to clean code package when clean status", agentID_);
        clearCodePackagePromise_.SetValue(StatusCode::SUCCESS);
        return;
    }

    for (auto codeReferInfoIter = codeReferInfos_->begin(); codeReferInfoIter != codeReferInfos_->end();) {
        auto now = static_cast<uint64_t>(std::time(nullptr));
        if (codeReferInfoIter->second.instanceIDs.empty() &&
            (now - codeReferInfoIter->second.lastAccessTimestamp >=
             static_cast<uint64_t>(codePackageThresholds_.codeagingtime()))) {
            const std::string &objectFile = codeReferInfoIter->first;
            ASSERT_IF_NULL(codeReferInfoIter->second.deployer);
            if (codeReferInfoIter->second.deployer->Clear(objectFile, objectFile)) {
                codeReferInfoIter = codeReferInfos_->erase(codeReferInfoIter);
                continue;
            }
        }
        (void)++codeReferInfoIter;
    }
    if (remainedClearCodePackageRetryTimes_ >= 0) {
        (void)--remainedClearCodePackageRetryTimes_;
    }
    clearCodePackageTimer_ =
        litebus::AsyncAfter(clearCodePackageInterval_, GetAID(), &AgentServiceActor::RemoveCodePackageAsync);
}

void AgentServiceActor::CommitSuicide()
{
    (void)clearCodePackagePromise_.GetFuture().OnComplete(
        [isUnitTestSituation(this->isUnitTestSituation_)](const litebus::Future<StatusCode> &) -> void {
            if (!isUnitTestSituation) {
                YR_EXIT("function agent suicide");
            }
        });
}

void AgentServiceActor::Finalize()
{
    UpdateAgentStatusToLocal(static_cast<int32_t>(FUNC_AGENT_EXITED), "function_agent exited");
    remainedClearCodePackageRetryTimes_ = 0;
    (void)litebus::TimerTools::Cancel(clearCodePackageTimer_);
}

void AgentServiceActor::CleanRuntimeManagerStatus(uint32_t retryTimes)
{
    if (sendCleanStatusPromise_.GetFuture().IsOK()) {
        return;
    }
    (void)++retryTimes;
    if (retryTimes > MAX_RETRY_SEND_CLEAN_STATUS_TIMES) {
        YRLOG_ERROR("{}|Send clean status to runtime manager({}) time out", agentID_, registerRuntimeMgr_.id);
        sendCleanStatusPromise_.SetValue(StatusCode::RUNTIME_MANAGER_CLEAN_STATUS_RESPONSE_TIME_OUT);
        return;
    }
    messages::CleanStatusRequest cleanStatusRequest;
    cleanStatusRequest.set_name(registerRuntimeMgr_.id);
    YRLOG_INFO("{}|Send clean status to runtime manager({})", agentID_, registerRuntimeMgr_.id);
    (void)Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "CleanStatus",
               cleanStatusRequest.SerializeAsString());

    (void)litebus::AsyncAfter(retrySendCleanStatusInterval_, GetAID(), &AgentServiceActor::CleanRuntimeManagerStatus,
                              retryTimes);
}

void AgentServiceActor::CleanStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_DEBUG("{}|receive CleanStatus from local-scheduler, function-agent gonna to suicide", agentID_);
    messages::CleanStatusRequest cleanStatusRequest;
    if (!cleanStatusRequest.ParseFromString(msg)) {
        YRLOG_ERROR("{}|failed to parse local-scheduler({}) CleanStatus message", agentID_, from.HashString());
        return;
    }

    if (cleanStatusRequest.name() != agentID_) {
        YRLOG_WARN("{}|receive wrong CleanStatus message from local-scheduler({})", agentID_, from.Name());
        return;
    }

    isCleaningStatus_ = true;
    remainedClearCodePackageRetryTimes_ = DEFAULT_RETRY_CLEAR_CODE_PACKAGER_TIMES;
    ASSERT_IF_NULL(codeReferInfos_);
    for (auto &codeReferInfoIter : (*codeReferInfos_)) {
        codeReferInfoIter.second.instanceIDs.clear();
    }

    messages::CleanStatusResponse cleanStatusResponse;
    (void)Send(from, "CleanStatusResponse", cleanStatusResponse.SerializeAsString());

    litebus::Async(GetAID(), &AgentServiceActor::CleanRuntimeManagerStatus, 0);
    (void)sendCleanStatusPromise_.GetFuture().OnComplete(litebus::Defer(GetAID(), &AgentServiceActor::CommitSuicide));
}

void AgentServiceActor::CleanStatusResponse(const litebus::AID &from, std::string &&, std::string &&)
{
    YRLOG_DEBUG("{}|receive CleanStatusResponse from runtime-manager ({})", agentID_, from.HashString());
    if (isCleaningStatus_) {
        sendCleanStatusPromise_.SetValue(StatusCode::SUCCESS);
    }
}

void AgentServiceActor::UpdateCred(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered) {
        YRLOG_ERROR("{}|registration is not complete, ignore query instance status info.", agentID_);
        return;
    }
    (void)Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "UpdateCred", std::move(msg));
}

void AgentServiceActor::UpdateCredResponse(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!isRegisterCompleted_) {
        YRLOG_ERROR("{}|registration is not complete, ignore query instance status response.", agentID_);
        return;
    }
    (void)Send(localSchedFuncAgentMgrAID_, "UpdateCredResponse", std::move(msg));
}

void AgentServiceActor::GracefulShutdownFinish(const litebus::AID &, std::string &&, std::string &&msg)
{
    YRLOG_ERROR("receive graceful shutdown finish from runtime manager");
    runtimeManagerGracefulShutdown_.SetValue(true);
}

litebus::Future<bool> AgentServiceActor::GracefulShutdown()
{
    YRLOG_ERROR("graceful shutdown agent service, gracefulShutdownTime: {}", gracefulShutdownTime_);
    CleanRuntimeManagerStatus(0);
    (void)litebus::TimerTools::AddTimer(gracefulShutdownTime_ * GRACE_SHUTDOWN_TIMEOUT_MS, GetAID(),
                                        [promise(runtimeManagerGracefulShutdown_)]() { promise.SetValue(true); });
    return runtimeManagerGracefulShutdown_.GetFuture();
}

litebus::Future<Status> AgentServiceActor::IsRegisterLocalSuccessful()
{
    return registerInfo_.registeredPromise.GetFuture().Then([](const messages::Registered &) { return Status::OK(); });
}

void AgentServiceActor::QueryDebugInstanceInfos(const litebus::AID &, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered) {
        YRLOG_ERROR("{}|registration is not complete, ignore query debug instatnce infos.", agentID_);
        return;
    }

    (void)Send(litebus::AID(registerRuntimeMgr_.name, registerRuntimeMgr_.address), "QueryDebugInstanceInfos",
               std::move(msg));
}

void AgentServiceActor::QueryDebugInstanceInfosResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    if (!registerRuntimeMgr_.registered || !isRegisterCompleted_) {
        YRLOG_ERROR("agent({}) registration is not complete, ignore query debug instatnce infos response.", agentID_);
        return;
    }

    messages::QueryDebugInstanceInfosResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid debug instance response from({}), {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("{}|got instance status response from({}), {}", rsp.requestid(), std::string(from),
                rsp.ShortDebugString());
    (void)Send(localSchedFuncAgentMgrAID_, "QueryDebugInstanceInfosResponse", std::move(msg));
}

void AgentServiceActor::SetNetworkIsolationRequest(const litebus::AID &, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::SetNetworkIsolationRequest>();
    messages::SetNetworkIsolationResponse resp;
    req->ParseFromString(msg);
    resp.set_requestid(req->requestid());
    YRLOG_DEBUG("agent receive SetNetworkIsolationRequest({})", req->requestid());
    resp.set_message("ipset not exist");
    resp.set_code(static_cast<int32_t>(StatusCode::FAILED));
    (void)Send(localSchedFuncAgentMgrAID_, "SetNetworkIsolationResponse", resp.SerializeAsString());
}

bool AgentServiceActor::IsDelegateWorkingDirPath(const DeployerParameters &deployObject)
{
    // if th bucket id (working dir) is the code destination path, the deployObject is for a delegate working dir code
    return deployObject.request->deploymentconfig().bucketid() == deployObject.destination;
}

}  // namespace functionsystem::function_agent