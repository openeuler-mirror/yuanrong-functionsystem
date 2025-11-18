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

#include "bootstrap_actor.h"

#include <sys/inotify.h>

#include "function_master/system_function_loader/constants.h"
#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/constants/constants.h"
#include "common/constants/signal.h"
#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/types/instance_state.h"
#include "common/hex/hex.h"
#include "common/utils/files.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/struct_transfer.h"
#include "utils/string_utils.hpp"

namespace functionsystem::system_function_loader {
const std::string BOOTSTRAP_CONFIG_PATH = "/home/sn/function/config/system-function-config.json";  // NOLINT
const std::string METAFILE_WATCH_PATH = "/home/sn/function/system-function-meta";
const std::string SYSFUNCTION_CONFIG_KEY = "/faas/system-function/config";
const uint64_t SENDARGS_TIMEOUT_MS = 120000;
const uint64_t UPGRADE_TIMEOUT_MS = 60000;
const uint32_t WAIT_INSTANCE_MAX_TIMES = 60;
const int MAX_RETRY_TIMES = 5;
const int RETRY_TIMEOUT_MS = 10000;
const int MAX_SYSFUNCTION_META_INFO = 6;

using namespace functionsystem::explorer;

BootstrapActor::BootstrapActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                               std::shared_ptr<global_scheduler::GlobalSched> globalSched, uint32_t sysFuncRetryPeriod,
                               const std::string &instanceManagerAddress, bool enableFrontendPool)
    : ActorBase(SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR), retryTimeoutMs_(RETRY_TIMEOUT_MS)
{
    member_ = std::make_shared<Member>();
    member_->globalSched = std::move(globalSched);
    member_->sysFuncRetryPeriod = sysFuncRetryPeriod;
    member_->instanceManagerAddress = instanceManagerAddress;
    metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaClient);
    if (enableFrontendPool) {
        member_->dynamicFunctionSet.insert(FRONTEND_FUNCTION_NAME);
    }
}

void BootstrapActor::Init()
{
    ASSERT_IF_NULL(member_);
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this(), member_);
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this(), member_);

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "systemFunction", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &BootstrapActor::UpdateLeaderInfo, leaderInfo);
        });

    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;

    Receive("ForwardCustomSignalResponse", &BootstrapActor::ForwardCustomSignalResponse);
}

void BootstrapActor::LoadBootstrapConfig(const std::string &customArgs)
{
    (void)LoadSysFuncCustomArgs(customArgs);  // errors had been printed

    if (auto status = LoadCurrentSysFuncMetas(); status.IsError()) {
        return;
    }
    if (auto status = LoadSysFuncMetas(); status.IsError()) {
        return;
    }
    if (auto status = LoadSysFuncPayloads(); status.IsError()) {
        return;
    }
    if (auto status = LoadFunctionConfigs(); status.IsError()) {
        return;
    }
    // When the configuration cannot be loaded from storage, it may indicate that it is the first boot
    if (auto status = LoadCurrentFunctionConfigs(); status.IsError()) {
        YRLOG_INFO("First time to load system functions");
        member_->currFunctionConfigMap = member_->functionConfigMap;
    }

    SystemFunctionKeepAlive();

    // check whether config changed
    UpdateConfigHandler();
    UpdateSysFunctionPayload();
}

void BootstrapActor::SysFunctionConfigCallBack(const std::string &path, const std::string &name, uint32_t mask)
{
    YRLOG_DEBUG("path: {}, name: {}, mask: {}", path, name, mask);
    if (mask & IN_DELETE) {
        YRLOG_INFO("ReloadBootstrapConfig");
        litebus::AsyncAfter(waitUpdateConfigMapMs_, GetAID(), &BootstrapActor::UpdateConfigHandler);
    }
}

void BootstrapActor::SysFunctionPayloadCallBack(const std::string &path, const std::string &name, uint32_t mask)
{
    YRLOG_DEBUG("path: {}, name: {}, mask: {}", path, name, mask);
    if (mask & IN_DELETE) {
        YRLOG_INFO("ReloadSysFuncPayloads");
        litebus::AsyncAfter(waitUpdateConfigMapMs_, GetAID(), &BootstrapActor::UpdatePayloadHandler);
    }
}

void BootstrapActor::SysFunctionMetaCallBack(const std::string &path, const std::string &name, uint32_t mask)
{
    YRLOG_DEBUG("path: {}, name: {}, mask: {}", path, name, mask);
    if (mask & IN_DELETE) {
        YRLOG_INFO("ReloadSysFuncMetas");
        litebus::AsyncAfter(waitUpdateConfigMapMs_, GetAID(), &BootstrapActor::UpdateMetaHandler);
    }
}

void BootstrapActor::UpdateConfigHandler()
{
    if (auto status = LoadFunctionConfigs(); status.IsError()) {
        return;
    }
    UpdateSysFunctionConfig();
}

void BootstrapActor::UpdatePayloadHandler()
{
    systemFuncPayloadMap_.clear();
    if (auto status = LoadSysFuncPayloads(); status.IsError()) {
        return;
    }
    UpdateSysFunctionPayload();
}
void BootstrapActor::UpdateMetaHandler()
{
    LoadSysFuncMetas();
}

void BootstrapActor::UpdateSysFunctionConfig()
{
    for (auto iter : member_->functionConfigMap) {
        std::string funcName = iter.first;
        auto newConfig = GetFunctionConfig(funcName);
        if (newConfig.IsNone()) {
            YRLOG_ERROR("temp function config of ({}) does not exist", funcName);
            continue;
        }
        auto currConfig = GetCurrFunctionConfig(funcName);
        if (CheckSysFunctionNeedUpgrade(funcName, newConfig.Get(), currConfig)) {
            UpgradeSystemFunction(funcName, newConfig.Get(), currConfig);
            continue;
        }
    }
}

void BootstrapActor::UpdateSysFunctionPayload()
{
    for (auto iter = systemFuncPayloadMap_.begin(); iter != systemFuncPayloadMap_.end(); ++iter) {
        // wait for dynamic scale request from scaler
        if (member_->dynamicFunctionSet.find(iter->first) != member_->dynamicFunctionSet.end()
            && member_->dynamicFunctionInstance.find(iter->first) == member_->dynamicFunctionInstance.end()) {
            continue;
        }
        SendUpdateArgsSignal(iter->first, iter->second, 0);
    }
}

void BootstrapActor::UpdateSysFunctionPayloadByName(const std::string &functionName)
{
    for (auto iter = systemFuncPayloadMap_.begin(); iter != systemFuncPayloadMap_.end(); ++iter) {
        if (std::strcmp(iter->second.sysFuncName.c_str(), functionName.c_str()) == 0) {
            SendUpdateArgsSignal(iter->first, iter->second, 0);
        }
    }
}

void BootstrapActor::DoDynamicScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                                  const FunctionPayload &payload)
{
    YRLOG_INFO("dynamic scale function({}) instanceNum to {}", funcName, instanceNum);
    SendUpdateArgsSignal(funcName, payload, 0);
}

void BootstrapActor::UpdateSysFunctionMeta(const std::string &jsonStr)
{
    auto funcMeta = GetFuncMetaFromJson(jsonStr);
    // key:/yr/functions/business/yrk/tenant/0/function/0-system-faasscheduler/version/v1
    auto funcKey = FUNC_META_PATH_PREFIX + "/" + funcMeta.funcMetaData.tenantId + "/function/" +
                   funcMeta.funcMetaData.name + "/version/" + funcMeta.funcMetaData.version;
    auto funcName = funcMeta.funcMetaData.name;
    auto funcMetaQueueIter = systemFuncMetaQueue_.find(funcName);
    // load initial meta info for system function
    if (funcMetaQueueIter == systemFuncMetaQueue_.end()) {
        std::vector<std::pair<std::string, FunctionMeta>> queue;
        queue.emplace_back(funcKey, funcMeta);
        systemFuncMetaQueue_[funcName] = queue;
        metaStorageAccessor_->Put(funcKey, jsonStr);
        return;
    }
    // find same version in queue, move the version to the end of queue
    for (auto iter = funcMetaQueueIter->second.begin(); iter != funcMetaQueueIter->second.end();) {
        if (std::strcmp(iter->second.funcMetaData.version.c_str(), funcMeta.funcMetaData.version.c_str()) == 0) {
            systemFuncMetaQueue_[funcName].erase(iter);
            systemFuncMetaQueue_[funcName].emplace_back(funcKey, funcMeta);
            return;
        }
        ++iter;
    }
    // add new version at the end of queue, remove outdated version and update etcd
    systemFuncMetaQueue_[funcName].emplace_back(funcKey, funcMeta);
    metaStorageAccessor_->Put(funcKey, jsonStr);
    if (systemFuncMetaQueue_[funcName].size() > MAX_SYSFUNCTION_META_INFO) {
        metaStorageAccessor_->Delete(systemFuncMetaQueue_[funcName].front().first);
        systemFuncMetaQueue_[funcName].erase(systemFuncMetaQueue_[funcName].begin());
    }
}

void BootstrapActor::UpgradeSystemFunction(const std::string &funcName, const FunctionConfig &newConfig,
                                           const litebus::Option<FunctionConfig> &currConfigOpt)
{
    StopSystemFunctionKeepAlive();
    // Generally, when the current effective configuration is empty, the corresponding function instance does not exist.
    // In normal cases, you do not need to delete the instance and pull it again. However, to improve fault tolerance,
    // for example, if the current configuration in etcd is manually deleted, you still need to try to reclaim the
    // instance.
    FunctionConfig currConfig = newConfig;
    if (currConfigOpt.IsNone()) {
        currConfig = newConfig;
    }
    YRLOG_INFO("Start to upgrade function ({})", funcName);
    (void)SendUpgradeFunctionSignal(funcName, newConfig, currConfig, 0)
        .OnComplete(litebus::Defer(GetAID(), &BootstrapActor::SendUpgradeFunctionSignalCallBack, funcName, newConfig,
                                   std::placeholders::_1));
}

void BootstrapActor::SendUpgradeFunctionSignalCallBack(const std::string &funcName, const FunctionConfig &newConfig,
                                                       const litebus::Future<Status> &future)
{
    if (future.IsError() || future.Get().IsError()) {
        YRLOG_WARN("function ({}) upgrade failed, rollback to previous version", funcName);
    } else {
        YRLOG_INFO("function ({}) upgrade succeed, save new bootstrap config", funcName);
        member_->currFunctionConfigMap[funcName] = newConfig;
        metaStorageAccessor_->Put(SYSFUNCTION_CONFIG_KEY + "/" + funcName, newConfig.jsonStr);
    }
    litebus::Async(GetAID(), &BootstrapActor::SystemFunctionKeepAlive);
    litebus::AsyncAfter(waitStartInstanceMs_, GetAID(), &BootstrapActor::UpdateSysFunctionPayloadByName, funcName);
}

litebus::Future<std::list<Status>> BootstrapActor::KillSystemFuncInstances()
{
    ASSERT_IF_NULL(business_);
    return business_->KillSystemFuncInstances();
}

litebus::Future<Status> BootstrapActor::KillInstance(const FuncInstanceParams &instanceParams, const int32_t signal,
                                                     const std::string functionName)
{
    auto instanceKeyOpt =
        GenInstanceKey(instanceParams.functionKey, instanceParams.instanceID, instanceParams.requestID);
    if (instanceKeyOpt.IsNone()) {
        YRLOG_ERROR("failed to find instance({}), invalid instance key", instanceParams.instanceID);
        return Status(FAILED);
    }

    auto instanceKey = instanceKeyOpt.Get();
    // get proxy id from metastore
    auto instance = metaStorageAccessor_->GetWithPrefix(instanceKey);
    if (instance.IsNone()) {
        YRLOG_WARN("instance({}) not existed", instanceKey);
        return signal == SHUT_DOWN_SIGNAL_SYNC ? Status(SUCCESS) : Status(FAILED);
    }

    auto instancePtr = std::make_shared<resource_view::InstanceInfo>();
    if (!TransToInstanceInfoFromJson(*instancePtr, instance.Get().second)) {
        YRLOG_WARN("invalid instance({}) body from meta storage", instanceKey);
        return signal == SHUT_DOWN_SIGNAL_SYNC ? Status(SUCCESS) : Status(FAILED);
    }

    // if instance has been token over by instance-manager, can't kill through function-proxy, delete instance info in
    // meta store directly
    if (instancePtr->functionproxyid().empty() || instancePtr->functionproxyid() == INSTANCE_MANAGER_OWNER) {
        YRLOG_DEBUG("instance({}) has been taken over by instance-manager, send kill to instance manager", instanceKey);
        messages::ForwardKillRequest req;
        req.set_requestid(instancePtr->requestid());
        *req.mutable_instance() = std::move(*instancePtr);
        (void)Send(litebus::AID(INSTANCE_MANAGER_ACTOR_NAME, member_->instanceManagerAddress), "ForwardKill",
                   req.SerializeAsString());
        return Status(FAILED);
    }

    // get proxy address from global
    return member_->globalSched->GetLocalAddress(instancePtr->functionproxyid())
        .Then(litebus::Defer(GetAID(), &BootstrapActor::SendKillRequest, std::placeholders::_1, instanceParams,
                             instancePtr->functionproxyid(), signal, functionName));
}

litebus::Future<Status> BootstrapActor::SendKillRequest(
    const litebus::Future<litebus::Option<std::string>> &proxyAddress, const FuncInstanceParams &instanceParams,
    const std::string &proxyID, const int32_t signal, const std::string &functionName)
{
    if (proxyAddress.IsError() || proxyAddress.Get().IsNone()) {
        YRLOG_ERROR("failed to kill instance({}), failed to get local address from global scheduler",
                    instanceParams.instanceID);
        return Status(FAILED);
    }

    auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    core_service::KillRequest killRequest{};
    killRequest.set_instanceid(instanceParams.instanceID);
    killRequest.set_signal(signal);
    killRequest.set_requestid(requestID);
    YRLOG_INFO("set killRequest signal: {}", signal);
    if (signal != SHUT_DOWN_SIGNAL_SYNC) {
        YRLOG_INFO("set payload for args upgrade");
        if (functionName.empty() || systemFuncPayloadMap_.find(functionName) == systemFuncPayloadMap_.end()) {
            YRLOG_ERROR("function ({}) does not exist", functionName);
            return Status(FAILED);
        }
        auto payload = Base64Encode(std::string(systemFuncPayloadMap_[functionName].payload.dump()));
        killRequest.set_payload(payload);
    }

    auto forwardKillRequest = std::make_shared<internal::ForwardKillRequest>();
    forwardKillRequest->set_requestid(requestID);
    forwardKillRequest->set_srcinstanceid("");
    forwardKillRequest->set_instancerequestid(instanceParams.requestID);
    *forwardKillRequest->mutable_req() = std::move(killRequest);

    auto aid = litebus::AID(proxyID + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX, proxyAddress.Get().Get());
    YRLOG_INFO("{}|send instance({}) kill request to local({})", requestID, instanceParams.instanceID,
               std::string(aid));
    (void)Send(aid, "ForwardCustomSignalRequest", forwardKillRequest->SerializeAsString());

    auto promise = std::make_shared<litebus::Promise<Status>>();
    killPromiseMap_[requestID] = promise;
    return promise->GetFuture();
}

void BootstrapActor::ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum)
{
    auto iter = systemFuncPayloadMap_.find(funcName);
    if (iter == systemFuncPayloadMap_.end()) {
        return;
    }
    auto &payload = iter->second;
    payload.payload["instanceNum"] = instanceNum;
    member_->dynamicFunctionInstance[funcName] = instanceNum;
    ASSERT_IF_NULL(business_);
    return business_->ScaleByFunctionName(funcName, instanceNum, payload);
}

Status BootstrapActor::LoadFunctionConfigs()
{
    if (!litebus::os::ExistPath(BOOTSTRAP_CONFIG_PATH)) {
        YRLOG_WARN("bootstrap config file not exist, maybe not be supported.");
        return Status(StatusCode::FILE_NOT_FOUND, "bootstrap config file not exist");
    }
    std::string jsonStr = Read(BOOTSTRAP_CONFIG_PATH);

    nlohmann::json confJson;
    try {
        confJson = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("LoadFunctionConfigs parse json failed, error: {}", e.what());
        return Status(StatusCode::JSON_PARSE_ERROR, "parse json failed, " + jsonStr + ", error: " + e.what());
    }
    for (const auto &item : confJson.items()) {
        auto functionName = item.key();
        if (functionName.empty()) {
            YRLOG_WARN("empty function name");
            continue;
        }
        auto config = item.value();
        BuildConfigMap(member_->functionConfigMap, functionName, config);
    }

    return Status(StatusCode::SUCCESS);
}

Status BootstrapActor::LoadCurrentFunctionConfigs()
{
    auto bootstrapConfig = metaStorageAccessor_->GetAllWithPrefix(SYSFUNCTION_CONFIG_KEY);
    if (bootstrapConfig.IsNone()) {
        YRLOG_WARN("Failed to get bootstrap config from metaStorage");
        return Status(StatusCode::FAILED);
    }

    auto currentConfig = bootstrapConfig.Get();
    for (auto iter = currentConfig.begin(); iter != currentConfig.end(); ++iter) {
        std::string key = iter->first;
        std::string value = iter->second;
        std::string functionName = key.substr(key.find_last_of('/') + 1, key.length());
        nlohmann::json confJson;
        try {
            confJson = nlohmann::json::parse(value);
        } catch (nlohmann::detail::parse_error &e) {
            YRLOG_ERROR("parse json failed, error: {}", e.what());
            return Status(StatusCode::JSON_PARSE_ERROR, "parse json failed, " + value + ", error: " + e.what());
        }
        BuildConfigMap(member_->currFunctionConfigMap, functionName, confJson);
    }
    return Status(StatusCode::SUCCESS);
}

Status BootstrapActor::LoadSysFuncPayloads()
{
    if (!litebus::os::ExistPath(PAYLOADFILE_WATCH_PATH)) {
        YRLOG_WARN("{} is not exist", PAYLOADFILE_WATCH_PATH);
        return Status(StatusCode::FAILED);
    }

    auto filesOption = litebus::os::Ls(PAYLOADFILE_WATCH_PATH);
    if (filesOption.IsNone() || filesOption.Get().empty()) {
        YRLOG_WARN("no function payload file in {}", PAYLOADFILE_WATCH_PATH);
        return Status(StatusCode::FAILED);
    }

    auto files = filesOption.Get();
    for (const auto &file : files) {
        auto filePath = litebus::os::Join(PAYLOADFILE_WATCH_PATH, file, '/');
        if (!IsFile(filePath)) {
            continue;
        }

        YRLOG_INFO("read function payload file {}", filePath);
        auto content = litebus::os::Read(filePath);
        if (content.IsNone() || content.Get().empty()) {
            YRLOG_WARN("no function payload information in {}", filePath);
            continue;
        }

        std::string jsonStr = content.Get();
        nlohmann::json confJson;
        try {
            confJson = nlohmann::json::parse(jsonStr);
        } catch (nlohmann::detail::parse_error &e) {
            YRLOG_ERROR("LoadSysFuncPayloads parse json failed, error: {}", e.what());
            return Status(StatusCode::JSON_PARSE_ERROR, "parse json failed, " + jsonStr + ", error: " + e.what());
        }

        for (const auto &item : confJson.items()) {
            AddSysFuncPayload(item.key(), item.value());
        }
    }
    YRLOG_INFO("load system function payload from path({}) successfully", PAYLOADFILE_WATCH_PATH);
    return Status(StatusCode::SUCCESS);
}

void BootstrapActor::AddSysFuncPayload(const std::string &configKey, const nlohmann::json &config)
{
    if (configKey.empty()) {
        YRLOG_WARN("empty config key");
        return;
    }

    FunctionPayload funcPayload{ .sysFuncName = "", .signal = 0, .payload = {} };
    if (config.find("systemFunctionName") != config.end()) {
        funcPayload.sysFuncName = config["systemFunctionName"];
    }
    if (config.find("signal") != config.end()) {
        funcPayload.signal = config["signal"];
    }
    if (config.find("payload") != config.end()) {
        funcPayload.payload = config["payload"];
    }
    if (auto iter = member_->dynamicFunctionSet.find(configKey); iter != member_->dynamicFunctionSet.end()) {
        uint32_t instanceNum = 0;
        auto insIter = member_->dynamicFunctionInstance.find(configKey);
        if (insIter != member_->dynamicFunctionInstance.end()) {
            instanceNum = insIter->second;
        }
        // update instance num
        if (funcPayload.payload.find("instanceNum") != funcPayload.payload.end()) {
            funcPayload.payload["instanceNum"] = instanceNum;
        }
    }
    systemFuncPayloadMap_[configKey] = funcPayload;
}

Status BootstrapActor::LoadSysFuncMetas()
{
    if (!litebus::os::ExistPath(METAFILE_WATCH_PATH)) {
        YRLOG_WARN("{} is not exist", METAFILE_WATCH_PATH);
        return Status(StatusCode::FAILED);
    }

    auto filesOption = litebus::os::Ls(METAFILE_WATCH_PATH);
    if (filesOption.IsNone() || filesOption.Get().empty()) {
        YRLOG_WARN("no function meta file in {}", METAFILE_WATCH_PATH);
        return Status(StatusCode::FAILED);
    }

    auto files = filesOption.Get();
    for (const auto &file : files) {
        auto filePath = litebus::os::Join(METAFILE_WATCH_PATH, file, '/');
        if (!IsFile(filePath)) {
            continue;
        }

        YRLOG_INFO("Read function meta file {}", filePath);
        auto content = litebus::os::Read(filePath);
        if (content.IsNone() || content.Get().empty()) {
            YRLOG_WARN("no function meta information in {}", filePath);
            continue;
        }
        try {
            UpdateSysFunctionMeta(content.Get());
        } catch (std::exception &e) {
            YRLOG_WARN("function metadata is invalid in {}, error: {}", filePath, e.what());
            continue;
        }
    }
    YRLOG_INFO("load system function meta from path({}) successfully", METAFILE_WATCH_PATH);
    return Status(StatusCode::SUCCESS);
}

Status BootstrapActor::LoadCurrentSysFuncMetas()
{
    auto prefix = FUNC_META_PATH_PREFIX + "/0/function/";
    auto sysFuncMetas = metaStorageAccessor_->GetAllWithPrefix(prefix);
    if (sysFuncMetas.IsNone()) {
        YRLOG_WARN("No system function meta infos from metaStorage");
        return Status(StatusCode::SUCCESS);
    }

    auto currentMetas = sysFuncMetas.Get();
    for (auto iter = currentMetas.begin(); iter != currentMetas.end(); ++iter) {
        std::string funcKey = iter->first;
        std::string content = iter->second;
        try {
            auto funcMeta = GetFuncMetaFromJson(content);
            auto funcName = funcMeta.funcMetaData.name;
            if (systemFuncMetaQueue_.find(funcName) == systemFuncMetaQueue_.end()) {
                std::vector<std::pair<std::string, FunctionMeta>> queue;
                queue.emplace_back(funcKey, funcMeta);
                systemFuncMetaQueue_[funcName] = queue;
            } else {
                systemFuncMetaQueue_[funcName].emplace_back(funcKey, funcMeta);
            }
            if (systemFuncMetaQueue_[funcName].size() > MAX_SYSFUNCTION_META_INFO) {
                YRLOG_WARN("the number of recovered metadata ({}) reaches maximum limit ({})", funcName,
                           MAX_SYSFUNCTION_META_INFO);
            }
        } catch (std::exception &e) {
            YRLOG_WARN("function metadata is invalid with funcKey {}, error: {}", funcKey, e.what());
            continue;
        }
    }
    return Status(StatusCode::SUCCESS);
}

void BootstrapActor::BuildConfigMap(std::unordered_map<std::string, FunctionConfig> &mapName,
                                    const std::string &functionName, const nlohmann::json &config)
{
    YRLOG_INFO("Build function {} config", functionName);
    FunctionConfig functionConfig{ .tenantID = "0",
                                   .version = "$latest",
                                   .memory = 0,
                                   .cpu = 0,
                                   .instanceNum = 0,
                                   .args = {},
                                   .extension = {},
                                   .createOptions = {},
                                   .jsonStr = config.dump() };
    functionConfig.extension["schedule_policy"] = "shared";

    if (config.find("tenantID") != config.end()) {
        functionConfig.tenantID = config["tenantID"];
    }

    if (config.find("version") != config.end()) {
        functionConfig.version = config["version"];
    }

    if (config.find("memory") != config.end()) {
        functionConfig.memory = static_cast<float>(config["memory"]);
    }

    if (config.find("cpu") != config.end()) {
        functionConfig.cpu = static_cast<float>(config["cpu"]);
    }

    if (config.find("createOptions") != config.end()) {
        for (const auto &createOption : config["createOptions"].items()) {
            functionConfig.createOptions[createOption.key()] = createOption.value();
        }
    }

    if (config.find("instanceNum") != config.end()) {
        functionConfig.instanceNum = static_cast<float>(config["instanceNum"]);
    }

    if (auto schedulingOps = config.find("schedulingOps"); schedulingOps != config.end()) {
        if (auto extension = schedulingOps->find("extension"); extension != schedulingOps->end()) {
            for (const auto &item : extension->items()) {
                functionConfig.extension[item.key()] = item.value();
            }
        }
    }

    if (config.find("args") != config.end()) {
        functionConfig.args = config["args"];
        BuildFunctionArgs(functionName, functionConfig.args);
    }

    mapName[functionName] = functionConfig;
}

void BootstrapActor::BuildFunctionArgs(const std::string &functionName, nlohmann::json &customJson)
{
    auto customArgs = sysFuncCustomArgsMap_.find(functionName);
    if (customArgs == sysFuncCustomArgsMap_.end()) {
        YRLOG_INFO("function {} don't hava custom args in startup parameters", functionName);
        return;
    }

    if (customJson.empty() || !customJson.is_object()) {
        YRLOG_INFO("function {} don't hava custom args in config file", functionName);
        return;
    }

    for (const auto &customArg : customArgs->second.items()) {
        customJson[customArg.key()] = customArg.value();
    }
}

Status BootstrapActor::LoadSysFuncCustomArgs(const std::string &argStr)
{
    if (argStr.empty()) {
        YRLOG_WARN("sysFuncCustomArgs is empty");
        sysFuncCustomArgsMap_.clear();
        return Status(FAILED, "sysFuncCustomArgs is empty");
    }

    nlohmann::json argJson;
    try {
        argJson = nlohmann::json::parse(argStr);
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("parse arg json failed,error: {}", e.what());
        return Status(StatusCode::JSON_PARSE_ERROR, "parse arg json failed, " + argStr + ", error: " + e.what());
    }

    for (const auto &item : argJson.items()) {
        sysFuncCustomArgsMap_[item.key()] = item.value();
    }

    return Status(StatusCode::SUCCESS);
}

litebus::Future<bool> BootstrapActor::CheckInstanceExist(const FuncInstanceParams &funcInstanceParams)
{
    if (member_->instanceMgr == nullptr) {
        YRLOG_ERROR("failed to check instance exist, null instanceMgr");
        return false;
    }

    return member_->instanceMgr->GetInstanceInfoByInstanceID(funcInstanceParams.instanceID)
        .Then(litebus::Defer(GetAID(), &BootstrapActor::OnGetInstanceInfo, std::placeholders::_1, funcInstanceParams));
}

litebus::Future<bool> BootstrapActor::OnGetInstanceInfo(const instance_manager::InstanceKeyInfoPair &pair,
                                                        const FuncInstanceParams &funcInstanceParams)
{
    auto instanceKey =
        GenInstanceKey(funcInstanceParams.functionKey, funcInstanceParams.instanceID, funcInstanceParams.requestID);
    if (instanceKey.IsNone()) {
        YRLOG_WARN("invalid instance key");
        // don't need to re-create, return true
        return true;
    }

    auto key = pair.first;
    auto instancePtr = pair.second;
    if (key.empty() || instancePtr == nullptr) {
        YRLOG_INFO("instance {} 's status is not alive.", instanceKey.Get());
        member_->instanceWaitingStateTimesMap[instanceKey.Get()] = 0;
        return false;
    }

    auto routeKey = GenInstanceRouteKey(funcInstanceParams.instanceID);
    if (IsNonRecoverableStatus(instancePtr->instancestatus().code())) {
        YRLOG_INFO("instance({}) 's status is failed, try to kill", key);
        member_->instanceWaitingStateTimesMap[key] = 0;
        (void)KillInstance(funcInstanceParams, SHUT_DOWN_SIGNAL_SYNC);
        return true;
    }
    if (IsWaitingStatus(instancePtr->instancestatus().code())) {
        if (member_->instanceWaitingStateTimesMap.find(key) == member_->instanceWaitingStateTimesMap.end()) {
            member_->instanceWaitingStateTimesMap[key] = 0;
        }
        ++member_->instanceWaitingStateTimesMap[key];
    } else {
        member_->instanceWaitingStateTimesMap[key] = 0;
    }
    if (member_->instanceWaitingStateTimesMap[key] == WAIT_INSTANCE_MAX_TIMES) {
        YRLOG_INFO("instance({}) is in waiting status and reach max times, try to force delete", key);
        member_->instanceWaitingStateTimesMap[key] = 0;
        (void)metaStorageAccessor_->Delete(key, true);
        metaStorageAccessor_->Delete(routeKey, true);
        return true;
    }
    return true;
}

void BootstrapActor::OnCheckInstanceExist(const litebus::Future<bool> &isExisted, const FuncInstanceParams &params,
                                          const std::pair<std::string, FunctionConfig> &funcConfig)
{
    if (isExisted.IsOK() && isExisted.Get()) {
        return;
    }

    if (member_->isExiting) {
        YRLOG_INFO("system function loader is exiting, don't create new instance");
        return;
    }

    YRLOG_INFO("Send function {} schedule request to global scheduler", funcConfig.first);
    (void)member_->globalSched->Schedule(BuildScheduleRequest(funcConfig.second, params))
        .OnComplete([aid(GetAID()), functionName(funcConfig.first),
                     waitStartInstanceMs(waitStartInstanceMs_)](const litebus::Future<Status> &future) {
            if (future.IsError()) {
                YRLOG_ERROR("function {} schedule failed, reason: {}", functionName, future.GetErrorCode());
                return;
            }

            auto status = future.Get();
            if (status.IsError()) {
                YRLOG_ERROR("function {} schedule failed, reason: {}", functionName, status.ToString());
                return;
            }
            YRLOG_INFO("function {} schedule success", functionName);
            litebus::AsyncAfter(waitStartInstanceMs, aid, &BootstrapActor::UpdateSysFunctionPayloadByName,
                                functionName);
        });
}

std::shared_ptr<messages::ScheduleRequest> BootstrapActor::BuildScheduleRequest(
    const FunctionConfig &functionConfig, const FuncInstanceParams &funcInstanceParams)
{
    auto createRequest = std::make_shared<CreateRequest>();
    // traceID
    createRequest->set_traceid(funcInstanceParams.traceID);
    // requestID
    createRequest->set_requestid(funcInstanceParams.requestID);
    // instanceID
    createRequest->set_designatedinstanceid(funcInstanceParams.instanceID);
    // function
    createRequest->set_function(funcInstanceParams.functionKey);

    // args
    if (!functionConfig.args.empty()) {
        auto args = createRequest->mutable_args();
        ::common::Arg arg{};
        arg.set_type(::common::Arg_ArgType_VALUE);
        arg.set_value(functionConfig.args.dump());
        args->Add(std::move(arg));
    }

    // createOptions
    for (const auto &createOpt : functionConfig.createOptions) {
        (*createRequest->mutable_createoptions())[createOpt.first] = createOpt.second;
    }
    (*createRequest->mutable_createoptions())[RESOURCE_OWNER_KEY] = SYSTEM_OWNER_VALUE;  // system function label

    // schedulingOps.extension
    for (const auto &extension : functionConfig.extension) {
        (*createRequest->mutable_schedulingops()->mutable_extension())[extension.first] = extension.second;
    }

    // resources
    (*createRequest->mutable_schedulingops()->mutable_resources())[CPU_RESOURCE_NAME] = functionConfig.cpu;
    (*createRequest->mutable_schedulingops()->mutable_resources())[MEMORY_RESOURCE_NAME] = functionConfig.memory;

    auto scheduleRequest = TransFromCreateReqToScheduleReq(std::move(*createRequest), "");

    (*scheduleRequest->mutable_instance()->mutable_scheduleoption()->mutable_resourceselector())[RESOURCE_OWNER_KEY] =
        scheduleRequest->instance().instanceid();

    // set instance status to SCHEDULING for instances created by bootstrap,
    // avoid proxy forward schedule request to domain, when schedule conflict happen
    scheduleRequest->mutable_instance()->mutable_instancestatus()->set_code(
        static_cast<int32_t>(InstanceState::SCHEDULING));
    scheduleRequest->mutable_instance()->mutable_instancestatus()->set_msg("scheduling");
    scheduleRequest->mutable_instance()->set_issystemfunc(true);
    scheduleRequest->mutable_instance()->set_tenantid(functionConfig.tenantID.empty() ? "0" : functionConfig.tenantID);

    YRLOG_INFO("{}|schedule instance({})", scheduleRequest->requestid(), scheduleRequest->instance().instanceid());
    return scheduleRequest;
}

void BootstrapActor::SystemFunctionKeepAlive()
{
    ASSERT_IF_NULL(business_);
    business_->SystemFunctionKeepAlive();
    // loop
    keepAliveTimer_ =
        litebus::AsyncAfter(member_->sysFuncRetryPeriod, GetAID(), &BootstrapActor::SystemFunctionKeepAlive);
}

void BootstrapActor::StopSystemFunctionKeepAlive()
{
    litebus::TimerTools::Cancel(keepAliveTimer_);
}

FuncInstanceParams BootstrapActor::BuildFuncInstanceParams(const std::string &functionName,
                                                           const FunctionConfig &config, uint32_t index)
{
    std::string key = functionName + "-" + std::to_string(index);
    return FuncInstanceParams{ .traceID = key,
                               .instanceID = key,
                               .requestID = key,
                               .functionKey = config.tenantID + "/" + functionName + "/" + config.version };
}

litebus::Option<FunctionConfig> BootstrapActor::GetFunctionConfig(const std::string &funcName)
{
    if (member_->functionConfigMap.find(funcName) == member_->functionConfigMap.end()) {
        return litebus::None();
    }
    return member_->functionConfigMap[funcName];
}

litebus::Option<FunctionConfig> BootstrapActor::GetCurrFunctionConfig(const std::string &funcName)
{
    if (member_->currFunctionConfigMap.find(funcName) == member_->currFunctionConfigMap.end()) {
        return litebus::None();
    }
    return member_->currFunctionConfigMap[funcName];
}

size_t BootstrapActor::GetFunctionConfigSize()
{
    return member_->functionConfigMap.size();
}

litebus::Option<nlohmann::json> BootstrapActor::GetSysFuncCustomArgs(const std::string &funcName)
{
    if (sysFuncCustomArgsMap_.find(funcName) == sysFuncCustomArgsMap_.end()) {
        return {};
    }
    return sysFuncCustomArgsMap_[funcName];
}

litebus::Option<FunctionPayload> BootstrapActor::GetFunctionPayload(const std::string &funcName)
{
    if (systemFuncPayloadMap_.find(funcName) == systemFuncPayloadMap_.end()) {
        return {};
    }
    return systemFuncPayloadMap_[funcName];
}

litebus::Option<FunctionMetaQueue> BootstrapActor::GetFunctionMetaQueue()
{
    return systemFuncMetaQueue_;
}

void BootstrapActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR, leaderInfo.address);

    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    business_->OnChange();
    curStatus_ = newStatus;
}

void BootstrapActor::ForwardCustomSignalResponse(const litebus::AID &from, std::string &&, std::string &&msg)
{
    internal::ForwardKillResponse forwardKillResponse;
    if (msg.empty() || !forwardKillResponse.ParseFromString(msg)) {
        YRLOG_WARN("(custom signal)invalid response body from({}).", from.HashString());
        return;
    }

    auto iter(killPromiseMap_.find(forwardKillResponse.requestid()));
    if (iter == killPromiseMap_.end()) {
        YRLOG_WARN("{}|(custom signal)failed to get response, no request matches result",
                   forwardKillResponse.requestid());
        return;
    }
    YRLOG_DEBUG("{}|(custom signal) get response", forwardKillResponse.requestid());
    iter->second->SetValue(Status::OK());
    (void)killPromiseMap_.erase(forwardKillResponse.requestid());
}

void BootstrapActor::BindInstanceManager(const std::shared_ptr<instance_manager::InstanceManager> &instanceMgr)
{
    member_->instanceMgr = instanceMgr;
}

void BootstrapActor::MasterBusiness::SystemFunctionKeepAlive()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    for (const auto &functionConfig : member_->currFunctionConfigMap) {
        for (uint32_t i = 0; i < functionConfig.second.instanceNum; ++i) {
            auto params = BuildFuncInstanceParams(functionConfig.first, functionConfig.second, i);
            litebus::Async(actor->GetAID(), &BootstrapActor::CheckInstanceExist, params)
                .OnComplete(litebus::Defer(actor->GetAID(), &BootstrapActor::OnCheckInstanceExist,
                                           std::placeholders::_1, params, functionConfig));
        }
    }
}

void BootstrapActor::MasterBusiness::ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                                         const FunctionPayload &payload)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->DoDynamicScaleByFunctionName(funcName, instanceNum, payload);
}

bool BootstrapActor::CheckSysFunctionNeedUpgrade(const std::string &funcName, const FunctionConfig &newConfig,
                                                 const litebus::Option<FunctionConfig> &currConfigOpt)
{
    if (currConfigOpt.IsNone()) {
        YRLOG_INFO("current config is empty, need to upgrade function ({})", funcName);
        return true;
    }
    auto currConfig = currConfigOpt.Get();
    if (newConfig.version != currConfig.version) {
        YRLOG_INFO("version ({}) is different from current version ({}), need to upgrade function ({})",
                   newConfig.version, currConfig.version, funcName);
        return true;
    }

    // will check all createOptions
    if (newConfig.createOptions.size() != currConfig.createOptions.size()) {
        YRLOG_INFO("size of createOptions differs, need to upgrade function ({}), size before ({}), size after ({})",
                   funcName, currConfig.createOptions.size(), newConfig.createOptions.size());
        return true;
    }
    for (auto &[optKey, value] : newConfig.createOptions) {
        auto currIt = currConfig.createOptions.find(optKey);
        if (currIt == currConfig.createOptions.end()) {
            YRLOG_INFO("{} option is added, need to upgrade function ({})", optKey, funcName);
            return true;
        }
        if (std::strcmp(value.c_str(), currIt->second.c_str()) != 0) {
            YRLOG_INFO(
                "{} has different config, new({}), curr({}), need to upgrade function ({})",
                optKey, value, currIt->second, funcName);
            return true;
        }
    }

    if (abs(newConfig.memory - currConfig.memory) > EPSINON) {
        YRLOG_INFO("memory ({}) is different from current memory ({}), need to upgrade function ({})", newConfig.memory,
                   currConfig.memory, funcName);
        return true;
    }
    if (abs(newConfig.cpu - currConfig.cpu) > EPSINON) {
        YRLOG_INFO("cpu ({}) is different from current cpu ({}), need to upgrade function ({})", newConfig.cpu,
                   currConfig.cpu, funcName);
        return true;
    }

    if (std::strcmp(newConfig.args.dump().c_str(), currConfig.args.dump().c_str()) != 0) {
        YRLOG_INFO("args is different from current args, need to upgrade function ({})", funcName);
        return true;
    }
    return false;
}

void BootstrapActor::SendUpgradeFunctionScheduleRequest(const std::shared_ptr<litebus::Promise<Status>> &promise,
                                                        const FunctionConfig &newConfig,
                                                        const FuncInstanceParams &newParams,
                                                        uint32_t retryTime)
{
    if (retryTime > MAX_RETRY_TIMES) {
        YRLOG_WARN("Timeout to UpgradeFunctionSchedule, retry");
        promise->SetValue(Status(StatusCode::FAILED));
        return;
    }

    member_->globalSched->Schedule(BuildScheduleRequest(newConfig, newParams))
        .After(retryTimeoutMs_,
               [](const litebus::Future<Status> &future) {
                   future.SetFailed(-1);
                   return future;
               })
        .OnComplete([promise, newConfig, newParams, retryTime, aid(GetAID())](const litebus::Future<Status> &future) {
            if (future.IsError()) {
                YRLOG_WARN("Timeout to UpgradeFunctionSchedule, retry({})", retryTime);
                litebus::Async(aid, &BootstrapActor::SendUpgradeFunctionScheduleRequest, promise, newConfig, newParams,
                               retryTime + 1);
                return;
            }
            promise->SetValue(future.Get());
        });
}

void BootstrapActor::RetrySendUpgradeFunctionSignal(const std::string &functionName, const FunctionConfig &newConfig,
                                                    const FunctionConfig &currConfig, int retryTimes,
                                                    const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    promise->Associate(litebus::Async(GetAID(), &BootstrapActor::SendUpgradeFunctionSignal, functionName,
                                      newConfig, currConfig, retryTimes));
}

litebus::Future<Status> BootstrapActor::SendUpgradeFunctionSignal(const std::string &functionName,
                                                                  const FunctionConfig &newConfig,
                                                                  const FunctionConfig &currConfig, int retryTimes)
{
    if (retryTimes > MAX_RETRY_TIMES) {
        YRLOG_WARN("Failed to upgrade function ({}), reaches maximum retry times", functionName);
        return Status(StatusCode::FAILED);
    }

    auto newParams = BuildFuncInstanceParams(functionName, newConfig, 0);
    auto currParams = BuildFuncInstanceParams(functionName, currConfig, 0);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    KillInstance(currParams, SHUT_DOWN_SIGNAL_SYNC)
        .After(UPGRADE_TIMEOUT_MS, [currParams](const litebus::Future<Status> &future) -> litebus::Future<Status> {
            YRLOG_WARN("Kill instance ({}) timeout {} ms", currParams.functionKey, UPGRADE_TIMEOUT_MS);
            auto promise = std::make_shared<litebus::Promise<Status>>();
            promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return promise->GetFuture();
        })
        .Then([aid(GetAID()), currParams, newConfig, newParams, member(member_),
               waitKillInstanceMs(waitKillInstanceMs_)](const Status &status) {
            auto promise = std::make_shared<litebus::Promise<Status>>();
            if (status.IsError() && status.StatusCode() != ERR_INSTANCE_NOT_FOUND
                && status.StatusCode() != ERR_INSTANCE_EXITED) {
                YRLOG_WARN("Failed to kill instance({}), Code({})", currParams.functionKey,
                           fmt::underlying(status.StatusCode()));
                promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                return promise->GetFuture();
            }
            if (member->isExiting) {
                YRLOG_WARN("Failed to create new instance({}), system function loader is exiting",
                           newParams.functionKey);
                promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                return promise->GetFuture();
            }
            litebus::AsyncAfter(waitKillInstanceMs, aid, &BootstrapActor::SendUpgradeFunctionScheduleRequest, promise,
                                newConfig, newParams, 0);
            return promise->GetFuture();
        })
        .OnComplete([aid(GetAID()), promise, functionName, newConfig, currConfig,
                     retryTimes, retryTimeoutMs(retryTimeoutMs_)](const litebus::Future<Status> &future) {
            if (future.IsError() || future.Get().IsError()) {
                YRLOG_WARN("Failed to upgrade function({}) for {} times, retry to upgrade", functionName, retryTimes);
                litebus::AsyncAfter(retryTimeoutMs, aid, &BootstrapActor::RetrySendUpgradeFunctionSignal,
                                    functionName, newConfig, currConfig, retryTimes + 1, promise);
                return;
            }
            YRLOG_INFO("Succeed to upgrade function ({})", functionName);
            promise->SetValue(Status(StatusCode::SUCCESS));
        });

    return promise->GetFuture();
}

void BootstrapActor::RetrySendUpdateArgsSignal(const std::string &functionName, const FunctionPayload &functionPayload,
                                               int retryTimes, const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    promise->Associate(
        litebus::Async(GetAID(), &BootstrapActor::SendUpdateArgsSignal, functionName, functionPayload, retryTimes));
}

litebus::Future<Status> BootstrapActor::SendUpdateArgsSignal(const std::string &functionName,
                                                             const FunctionPayload &functionPayload, int retryTimes)
{
    if (retryTimes > MAX_RETRY_TIMES) {
        YRLOG_WARN("Failed to update function({}) args, reaches maximum retry times", functionName);
        return Status(StatusCode::FAILED);
    }

    if (member_->currFunctionConfigMap.find(functionPayload.sysFuncName) == member_->currFunctionConfigMap.end()) {
        YRLOG_WARN("Failed to update function({}) args, can't find system-function ({})", functionName,
                   functionPayload.sysFuncName);
        return Status(StatusCode::FAILED);
    }

    FunctionConfig functionConfig = member_->currFunctionConfigMap[functionPayload.sysFuncName];
    auto params = BuildFuncInstanceParams(functionPayload.sysFuncName, functionConfig, 0);
    auto promise = std::make_shared<litebus::Promise<Status>>();
    YRLOG_INFO("Start to send function ({}) args to instance ({}) with signal ({})", functionName, params.functionKey,
               functionPayload.signal);
    KillInstance(params, functionPayload.signal, functionName)
        .After(SENDARGS_TIMEOUT_MS, [aid(GetAID()), functionName,
                                     params](const litebus::Future<Status> &future) -> litebus::Future<Status> {
            YRLOG_WARN("Send function ({}) args to instance({}) timeout {} ms", functionName, params.functionKey,
                       SENDARGS_TIMEOUT_MS);
            auto promise = std::make_shared<litebus::Promise<Status>>();
            promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return promise->GetFuture();
        })
        .OnComplete([aid(GetAID()), promise, functionName, functionPayload, params,
                     retryTimes, retryTimeoutMs(retryTimeoutMs_)](const litebus::Future<Status> &future) {
            if (future.IsError() || future.Get().IsError()) {
                YRLOG_WARN("Failed to send function({}) args to instance({}) for {} times, retry to send", functionName,
                           params.functionKey, retryTimes);
                litebus::AsyncAfter(retryTimeoutMs, aid, &BootstrapActor::RetrySendUpdateArgsSignal, functionName,
                                    functionPayload, retryTimes + 1, promise);
                return;
            }
            YRLOG_INFO("Succeed to send function({}) args to instance ({})", functionName, params.functionKey);
            promise->SetValue(Status(StatusCode::SUCCESS));
        });
    return promise->GetFuture();
}

litebus::Future<std::list<Status>> BootstrapActor::MasterBusiness::KillSystemFuncInstances()
{
    member_->isExiting = true;
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    // Send
    std::list<litebus::Future<Status>> futures;
    for (const auto &functionConfig : member_->functionConfigMap) {
        for (uint32_t i = 0; i < functionConfig.second.instanceNum; ++i) {
            auto params = BuildFuncInstanceParams(functionConfig.first, functionConfig.second, i);
            futures.push_back(actor->KillInstance(params, SHUT_DOWN_SIGNAL_SYNC));
        }
    }
    return litebus::Collect<Status>(futures);
}

void BootstrapActor::SlaveBusiness::SystemFunctionKeepAlive()
{
}

void BootstrapActor::SlaveBusiness::ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                                        const FunctionPayload &payload)
{
}

litebus::Future<std::list<Status>> BootstrapActor::SlaveBusiness::KillSystemFuncInstances()
{
    litebus::Promise<std::list<Status>> promise;
    std::list<Status> list;
    promise.SetValue(list);
    return promise.GetFuture();
}
}  // namespace functionsystem::system_function_loader