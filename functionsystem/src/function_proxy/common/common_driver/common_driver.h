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

#ifndef FUNCTION_PROXY_COMMON_COMMONDRIVER_H
#define FUNCTION_PROXY_COMMON_COMMONDRIVER_H
#include "common/flags/flags.h"
#include "common/utils/module_driver.h"
#include "function_proxy/common/distribute_cache_client/ds_cache_client_impl.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "function_proxy/common/observer/observer_actor.h"
#include "function_proxy/common/posix_client/shared_client/posix_stream_manager_proxy.h"
#include "function_proxy/common/posix_service/posix_service.h"
#include "function_proxy/common/state_handler/state_actor.h"

namespace functionsystem::function_proxy {
class CommonDriver : public ModuleDriver {
public:
    explicit CommonDriver(const function_proxy::Flags &flags, const std::shared_ptr<DSAuthConfig> &dsAuthConfig)
        : flags_(flags), dsAuthConfig_(dsAuthConfig), posixService_(std::make_shared<functionsystem::PosixService>())
    {
    }
    ~CommonDriver() override = default;

    Status Init();
    Status Start() override;
    Status Stop() override;
    Status Sync() override;
    void Await() override;

    inline std::shared_ptr<MetaStoreClient> GetMetaStoreClient()
    {
        return metaStoreClient_;
    }

    inline std::shared_ptr<DataInterfaceClientManagerProxy> GetDataInterfaceClientManagerProxy()
    {
        return dataInterfaceClient_;
    }

    inline std::shared_ptr<ControlInterfaceClientManagerProxy> GetControlInterfaceClientManagerProxy()
    {
        return controlInterfaceClient_;
    }

    inline std::shared_ptr<ObserverActor> GetObserverActor()
    {
        return observerActor_;
    }

    inline std::shared_ptr<InternalIAM> GetInternalIAM()
    {
        return internalIAM_;
    }

    inline std::shared_ptr<PosixService> GetPosixService()
    {
        return posixService_;
    }

    inline std::shared_ptr<MetaStorageAccessor> GetMetaStorageAccessor()
    {
        return metaStorageAccessor_;
    }

    inline std::shared_ptr<DSCacheClientImpl> GetDistributedCacheClient()
    {
        return distributedCacheClient_;
    }

private:
    void CreateDataAndControlInterfaceClient();

    void InitDistributedCache();
    void CreateDistributedCacheClient();
    void BindStateActor();
    Status InitMetaStoreClient();

    Status CreateInternalIAM(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor);

    void InitObserver(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor);

    Flags flags_;
    std::shared_ptr<DSAuthConfig> dsAuthConfig_ {nullptr};
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };
    std::shared_ptr<DSCacheClientImpl> distributedCacheClient_{ nullptr };
    std::shared_ptr<InternalIAM> internalIAM_{ nullptr };
    std::shared_ptr<DataInterfaceClientManagerProxy> dataInterfaceClient_;
    std::shared_ptr<ControlInterfaceClientManagerProxy> controlInterfaceClient_;
    std::shared_ptr<ObserverActor> observerActor_{ nullptr };
    std::shared_ptr<StateActor> stateActor_{ nullptr };
    std::shared_ptr<PosixService> posixService_{ nullptr };
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
};
}  // namespace functionsystem::function_proxy
#endif  // FUNCTION_PROXY_COMMON_COMMONDRIVER_H
