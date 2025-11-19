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

#include "bootstrap_driver.h"

#include "function_master/system_function_loader/constants.h"
#include "async/async.hpp"

namespace functionsystem::system_function_loader {

Status BootstrapDriver::Start()
{
    YRLOG_INFO("Start Bootstrap, sysFuncRetryPeriod: {}, sysFuncCustomArgs: {}", param_.sysFuncRetryPeriod,
               param_.sysFuncCustomArgs);

    // create
    if (auto status(Create()); status != StatusCode::SUCCESS) {
        return status;
    }

    // spawn actor
    litebus::Spawn(bootstrapActor_);

    litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::BindInstanceManager, param_.instanceMgr);

    // load system function
    litebus::Async(bootstrapActor_->GetAID(), &BootstrapActor::LoadBootstrapConfig, param_.sysFuncCustomArgs);

    // start file monitor to watch system-function-config
    fileMonitor_ = std::make_shared<FileMonitor>();
    fileMonitor_->Start();
    std::function<void(const std::string &, const std::string &, uint32_t)> configCallback =
        [aid(bootstrapActor_->GetAID())](const std::string &path, const std::string &name, uint32_t mask) {
            litebus::Async(aid, &BootstrapActor::SysFunctionConfigCallBack, path, name, mask);
        };
    fileMonitor_->AddWatch(CONFIGFILE_WATCH_PATH, configCallback);

    std::function<void(const std::string &, const std::string &, uint32_t)> payLoadCallback =
        [aid(bootstrapActor_->GetAID())](const std::string &path, const std::string &name, uint32_t mask) {
            litebus::Async(aid, &BootstrapActor::SysFunctionPayloadCallBack, path, name, mask);
        };
    fileMonitor_->AddWatch(PAYLOADFILE_WATCH_PATH, payLoadCallback);

    std::function<void(const std::string &, const std::string &, uint32_t)> metaCallback =
        [aid(bootstrapActor_->GetAID())](const std::string &path, const std::string &name, uint32_t mask) {
            litebus::Async(aid, &BootstrapActor::SysFunctionMetaCallBack, path, name, mask);
        };
    fileMonitor_->AddWatch(METAFILE_WATCH_PATH, metaCallback);

    return Status(StatusCode::SUCCESS);
}

Status BootstrapDriver::Create()
{
    // create bootstrap actor
    bootstrapActor_ = std::make_shared<BootstrapActor>(metaClient_, param_.globalSched, param_.sysFuncRetryPeriod,
                                                       param_.masterAddress, param_.enableFrontendPool);
    if (bootstrapActor_ == nullptr) {
        return Status(StatusCode::FAILED, "create bootstrap actor failed.");
    }

    return Status(StatusCode::SUCCESS);
}

Status BootstrapDriver::Stop()
{
    ASSERT_IF_NULL(bootstrapActor_);
    if (fileMonitor_) {
        fileMonitor_->Stop();
        fileMonitor_ = nullptr;
    }
    litebus::Terminate(bootstrapActor_->GetAID());
    return Status(StatusCode::SUCCESS);
}

void BootstrapDriver::Await()
{
    ASSERT_IF_NULL(bootstrapActor_);
    litebus::Await(bootstrapActor_->GetAID());
}
}  // namespace functionsystem::system_function_loader