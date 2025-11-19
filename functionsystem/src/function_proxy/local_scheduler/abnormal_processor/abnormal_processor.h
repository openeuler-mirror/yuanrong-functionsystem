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

#ifndef LOCAL_SCHEDULER_ABNORMAL_PROCESSOR_ABNORMAL_PROCESSOR_H
#define LOCAL_SCHEDULER_ABNORMAL_PROCESSOR_ABNORMAL_PROCESSOR_H

#include <string>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/utils/actor_driver.h"
#include "meta_store_client/meta_store_client.h"
#include "common/utils/exec_utils.h"
#include "function_proxy/common/observer/control_plane_observer/control_plane_observer.h"
#include "local_scheduler/instance_control/instance_ctrl.h"

namespace functionsystem::local_scheduler {
const std::string ABNORMAL_ACTOR = "abnormal_processor";
class AbnormalProcessorActor : public BasisActor {
public:
    explicit AbnormalProcessorActor(const std::string &id);
    ~AbnormalProcessorActor() override = default;

    void Finalize() override;

    void BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer);

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl);

    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr);

    void BindRaiseWrapper(const std::shared_ptr<RaiseWrapper> &raiseWrapper);

    void BindMetaStoreClient(const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    litebus::Future<bool> CheckLocalSchedulerIsLegal();

    void SchedulerAbnormalWatcher(const std::vector<WatchEvent> &events);

    // for test
    void SetQueryInterval(int64_t queryIntervalMs)
    {
        queryIntervalMs_ = queryIntervalMs;
    }

private:
    litebus::Future<bool> WatchAbnormal(const std::string &selfKey);
    void SchedulerAbnormaled(const std::shared_ptr<litebus::Promise<bool>> abnormaled);

    bool OnSchedulerAbnormaled(const std::vector<std::string> &localInstances,
                               const std::shared_ptr<litebus::Promise<bool>> abnormaled);

    void CommitSuicide();

    litebus::Future<SyncResult> AbnormalSyncer(const std::shared_ptr<GetResponse> &getResponse);

    std::string id_;
    std::shared_ptr<function_proxy::ControlPlaneObserver> observer_{ nullptr };
    std::shared_ptr<InstanceCtrl> instanceCtrl_{ nullptr };
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };
    std::shared_ptr<RaiseWrapper> raiseWrapper_{ nullptr };
    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_ {nullptr};
    litebus::Timer abnormalWatchTimer_{};
    int64_t queryIntervalMs_;
};

class AbnormalProcessor : public ActorDriver {
public:
    explicit AbnormalProcessor(const std::shared_ptr<AbnormalProcessorActor> &actor)
        : ActorDriver(actor), actor_(actor)
    {
    }

    inline static std::shared_ptr<AbnormalProcessor> Create(const std::string &id)
    {
        return std::make_shared<AbnormalProcessor>(std::make_shared<AbnormalProcessorActor>(id));
    }

    ~AbnormalProcessor() override;

    void BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer);

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl);

    void BindRaiseWrapper(const std::shared_ptr<RaiseWrapper> &raiseWrapper);

    void BindMetaStoreClient(const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr);

    // all bind method should be called before Start
    void Start();

    litebus::Future<Status> Recover() override;

private:
    bool isStarted_ = false;
    std::shared_ptr<AbnormalProcessorActor> actor_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_ABNORMAL_PROCESSOR_ABNORMAL_PROCESSOR_H
