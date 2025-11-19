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

#ifndef FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR_H
#define FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR_H

#include <actor/actor.hpp>
#include <nlohmann/json.hpp>

#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/metadata/metadata.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/status/status.h"
#include "function_master/global_scheduler/global_sched.h"
#include "function_master/instance_manager/instance_manager.h"

namespace functionsystem::system_function_loader {
const uint64_t WAIT_KILLINSTANCE_MS = 3000;
const uint64_t WAIT_STARTINSTANCE_MS = 10000;
const uint64_t WAIT_UPDATE_CONFIGMAP_MS = 1000;

struct FunctionConfig {
    std::string tenantID;
    std::string version;
    float memory;
    float cpu;
    uint32_t instanceNum;
    nlohmann::json args;
    std::unordered_map<std::string, std::string> extension;
    std::unordered_map<std::string, std::string> createOptions;
    std::string jsonStr;
};

struct FuncInstanceParams {
    std::string traceID;
    std::string instanceID;
    std::string requestID;
    std::string functionKey;
};

struct FunctionPayload {
    std::string sysFuncName;

    /**
     * Customized by SystemFunction.
     * 65: for updating frontend
     * 66: for updating scheduler
     */
    int32_t signal;
    nlohmann::json payload;
};

using FunctionMetaQueue = std::unordered_map<std::string, std::vector<std::pair<std::string, FunctionMeta>>>;

class BootstrapActor : public litebus::ActorBase, public std::enable_shared_from_this<BootstrapActor> {
public:
    BootstrapActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                   std::shared_ptr<global_scheduler::GlobalSched> globalSched, uint32_t sysFuncRetryPeriod,
                   const std::string &instanceManagerAddress, bool enableFrontendPool = false);

    ~BootstrapActor() override = default;

    void BindInstanceManager(const std::shared_ptr<instance_manager::InstanceManager> &instanceMgr);

    void LoadBootstrapConfig(const std::string &customArgs);

    litebus::Future<std::list<Status>> KillSystemFuncInstances();

    // only for test
    Status LoadFunctionConfigs();
    Status LoadSysFuncCustomArgs(const std::string &argStr);
    Status LoadCurrentFunctionConfigs();
    Status LoadSysFuncPayloads();
    Status LoadSysFuncMetas();
    Status LoadCurrentSysFuncMetas();

    litebus::Future<bool> CheckInstanceExist(const FuncInstanceParams &funcInstanceParams);
    void OnCheckInstanceExist(const litebus::Future<bool> &isExisted, const FuncInstanceParams &params,
                              const std::pair<std::string, FunctionConfig> &funcConfig);

    litebus::Future<bool> OnGetInstanceInfo(const instance_manager::InstanceKeyInfoPair &pair,
                                            const FuncInstanceParams &funcInstanceParams);

    static std::shared_ptr<messages::ScheduleRequest> BuildScheduleRequest(
        const FunctionConfig &functionConfig, const FuncInstanceParams &funcInstanceParams);

    litebus::Option<FunctionConfig> GetFunctionConfig(const std::string &funcName);
    litebus::Option<nlohmann::json> GetSysFuncCustomArgs(const std::string &funcName);
    size_t GetFunctionConfigSize();
    litebus::Option<FunctionConfig> GetCurrFunctionConfig(const std::string &funcName);
    litebus::Option<FunctionPayload> GetFunctionPayload(const std::string &funcName);
    litebus::Option<FunctionMetaQueue> GetFunctionMetaQueue();

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    litebus::Future<Status> KillInstance(const FuncInstanceParams &instanceParams, const int32_t signal,
                                         const std::string functionName = "");

    /**
     * receive response of forward custom signal from other local scheduler
     * @param from: AID of instance ctrl
     * @param name: function name
     * @param msg: response data, type is ForwardKillResponse
     */
    void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&, std::string &&msg);
    void SysFunctionConfigCallBack(const std::string &path, const std::string &name, uint32_t mask);
    void SysFunctionPayloadCallBack(const std::string &path, const std::string &name, uint32_t mask);

    void SysFunctionMetaCallBack(const std::string &path, const std::string &name, uint32_t mask);
    void UpdateConfigHandler();
    void UpdatePayloadHandler();

    void UpdateMetaHandler();
    void UpdateSysFunctionConfig();
    void UpdateSysFunctionPayload();
    void UpdateSysFunctionPayloadByName(const std::string &functionName);

    void UpdateSysFunctionMeta(const std::string &jsonStr);
    void UpgradeSystemFunction(const std::string &funcName, const FunctionConfig &newConfig,
                               const litebus::Option<FunctionConfig> &currConfigOpt);
    void SendUpgradeFunctionScheduleRequest(const std::shared_ptr<litebus::Promise<Status>> &promise,
                                            const FunctionConfig &newConfig, const FuncInstanceParams &newParams,
                                            uint32_t retryTime);
    void RetrySendUpgradeFunctionSignal(const std::string &functionName, const FunctionConfig &newConfig,
                                        const FunctionConfig &currConfig, int retryTimes,
                                        const std::shared_ptr<litebus::Promise<Status>> &promise);
    litebus::Future<Status> SendUpgradeFunctionSignal(const std::string &functionName, const FunctionConfig &newConfig,
                                                      const FunctionConfig &currConfig, int retryTimes);
    void SendUpgradeFunctionSignalCallBack(const std::string &funcName, const FunctionConfig &newConfig,
                                           const litebus::Future<Status> &future);
    void RetrySendUpdateArgsSignal(const std::string &functionName, const FunctionPayload &functionPayload,
                                   int retryTimes, const std::shared_ptr<litebus::Promise<Status>> &promise);
    litebus::Future<Status> SendUpdateArgsSignal(const std::string &functionName,
                                                 const FunctionPayload &functionPayload, int retryTimes);

    bool CheckSysFunctionNeedUpgrade(const std::string &funcName, const FunctionConfig &newConfig,
                                     const litebus::Option<FunctionConfig> &currConfigOpt);

    void ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum);

    [[maybe_unused]] std::unordered_map<std::string, uint32_t> GetInstanceWaitingStateTimesMap()
    {
        return member_->instanceWaitingStateTimesMap;
    }

    uint64_t retryTimeoutMs_;
    uint64_t waitKillInstanceMs_ = WAIT_KILLINSTANCE_MS;
    uint64_t waitStartInstanceMs_ = WAIT_STARTINSTANCE_MS;
    uint64_t waitUpdateConfigMapMs_ = WAIT_UPDATE_CONFIGMAP_MS;

protected:
    void Init() override;

private:
    litebus::Future<Status> SendKillRequest(const litebus::Future<litebus::Option<std::string>> &proxyAddress,
                                            const FuncInstanceParams &instanceParams, const std::string &proxyID,
                                            const int32_t signal, const std::string &functionName);
    void SystemFunctionKeepAlive();

    void StopSystemFunctionKeepAlive();

    void BuildFunctionArgs(const std::string &functionName, nlohmann::json &customJson);  // optional

    void BuildConfigMap(std::unordered_map<std::string, FunctionConfig> &mapName, const std::string &functionName,
                        const nlohmann::json &confJson);

    static FuncInstanceParams BuildFuncInstanceParams(const std::string &functionName, const FunctionConfig &config,
                                                      uint32_t index);
    void AddSysFuncPayload(const std::string &configKey, const nlohmann::json &config);
    void DoDynamicScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                      const FunctionPayload &payload);

    struct Member {
        std::unordered_map<std::string, FunctionConfig> functionConfigMap{};
        std::unordered_map<std::string, FunctionConfig> currFunctionConfigMap{};
        std::unordered_map<std::string, uint32_t> instanceWaitingStateTimesMap{};
        std::shared_ptr<global_scheduler::GlobalSched> globalSched;
        std::shared_ptr<instance_manager::InstanceManager> instanceMgr;
        uint32_t sysFuncRetryPeriod{ 0 };
        std::string instanceManagerAddress;
        bool isExiting{ false };
        // for some system function such as frontend, instanceNum is dynamically adjusted
        std::unordered_set<std::string> dynamicFunctionSet{};
        std::unordered_map<std::string, uint32_t> dynamicFunctionInstance{};
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<BootstrapActor> &actor, const std::shared_ptr<Member> &member)
            : actor_(actor), member_(member)
        {
        }
        virtual ~Business() override = default;
        virtual void SystemFunctionKeepAlive() = 0;
        virtual litebus::Future<std::list<Status>> KillSystemFuncInstances() = 0;
        virtual void ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                         const FunctionPayload &payload) = 0;
        void OnChange() override{};

    protected:
        std::weak_ptr<BootstrapActor> actor_;
        std::shared_ptr<Member> member_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<BootstrapActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~MasterBusiness() override = default;
        void SystemFunctionKeepAlive() override;
        litebus::Future<std::list<Status>> KillSystemFuncInstances() override;
        void ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                 const FunctionPayload &payload) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<BootstrapActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~SlaveBusiness() override = default;
        void SystemFunctionKeepAlive() override;
        litebus::Future<std::list<Status>> KillSystemFuncInstances() override;
        void ScaleByFunctionName(const std::string &funcName, uint32_t instanceNum,
                                 const FunctionPayload &payload) override;
    };

    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;

    std::unordered_map<std::string, nlohmann::json> sysFuncCustomArgsMap_{};

    std::shared_ptr<Member> member_;

    std::shared_ptr<MasterBusiness> masterBusiness_;
    std::shared_ptr<SlaveBusiness> slaveBusiness_;
    std::string curStatus_;
    std::shared_ptr<Business> business_;
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> killPromiseMap_;

    litebus::Timer keepAliveTimer_;
    std::unordered_map<std::string, FunctionPayload> systemFuncPayloadMap_;
    std::unordered_map<std::string, std::vector<std::pair<std::string, FunctionMeta>>> systemFuncMetaQueue_;
};

}  // namespace functionsystem::system_function_loader

#endif  // FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR_H
