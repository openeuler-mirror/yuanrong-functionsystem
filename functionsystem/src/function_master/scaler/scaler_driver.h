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

#ifndef FUNCTION_MASTER_SCALER_DRIVER_H
#define FUNCTION_MASTER_SCALER_DRIVER_H

#include "common/kube_client/api/apps_v1_api.h"
#include "actor/actor.hpp"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "function_master/common/flags/flags.h"
#include "function_master/global_scheduler/global_sched.h"
#include "function_master/scaler/scaler_actor.h"
#include "common/kube_client/kube_client.h"

namespace functionsystem::scaler {
class ScalerDriver : public ModuleDriver {
public:
    ScalerDriver(const functionsystem::functionmaster::Flags &flags,
                 const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                 const std::shared_ptr<MetaStoreClient> &upgradeWatchClient,
                 const std::shared_ptr<KubeClient> &kubeClient,
                 const ScalerHandlers &handlers);

    ~ScalerDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

private:
    functionsystem::functionmaster::Flags flags_;
    std::shared_ptr<ScalerActor> actor_ = nullptr;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_ = nullptr;
    std::shared_ptr<MetaStorageAccessor> systemUpgradeWatcher_ = nullptr;
    ScalerHandlers handlers_;
    std::shared_ptr<KubeClient> client_ = nullptr;
};  // class ScalerDriver
}  // namespace functionsystem::scaler

#endif  // FUNCTION_MASTER_SCALER_DRIVER_H
