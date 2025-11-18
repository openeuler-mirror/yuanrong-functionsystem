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

#include "etcd_observer.h"

#include <utility>

#include "common/logs/logging.h"
#include "meta_store_client/utils/etcd_util.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem {
EtcdObserver::EtcdObserver(std::string name, std::function<void(LeaderResponse)> callback,
                           const std::shared_ptr<::grpc::Channel> &channel, const std::string &etcdTablePrefix)
    : Observer(name, callback, etcdTablePrefix), channel_(channel)
{
}

EtcdObserver::~EtcdObserver()
{
    Stop();

    observeThread_ = nullptr;
    observeReader_ = nullptr;
    observeContext_ = nullptr;
}

Status EtcdObserver::Start()
{
    running_ = true;
    observeThread_ = std::make_unique<std::thread>([this]() { OnObserve(); });

    return Status::OK();
}

Status EtcdObserver::Init()
{
    v3electionpb::LeaderRequest request;
    request.set_name(etcdTablePrefix_ + name_);
    observeContext_ = std::make_unique<grpc::ClientContext>();
    observeReader_ = v3electionpb::Election::NewStub(channel_)->Observe(&(*observeContext_), request);
    if (observeReader_ == nullptr) {
        YRLOG_ERROR("explorer-trace|failed to observe key: {}", name_);
        return Status(StatusCode::FAILED, "failed to observe key: " + name_);
    }

    return Status::OK();
}

void EtcdObserver::OnObserve()
{
    YRLOG_INFO("explorer-trace|start a thread to read {} observer's stream", name_);
    while (running_) {
        if (auto status = Init(); status.IsOk()) {
            break;
        }
        YRLOG_ERROR("explorer-trace|failed to init {}, retry", name_);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    v3electionpb::LeaderResponse rsp;
    while (running_) {
        bool ok = observeReader_->Read(&rsp);
        if (!ok && running_) {
            YRLOG_ERROR("explorer-trace|failed to observer {}, reconnect", name_);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            (void)Init();
            continue;
        }

        if (!ok || !running_) {
            break;
        }

        LeaderResponse response;
        meta_store::Transform(response.header, rsp.header());
        response.kv.first = TrimKeyPrefix(rsp.kv().key(), etcdTablePrefix_);
        response.kv.second = rsp.kv().value();
        callback_(std::move(response));
    }
    YRLOG_INFO("explorer-trace|end the thread to read {} observer's stream", name_);
}

void EtcdObserver::Stop() noexcept
{
    if (!running_) {
        return;
    }

    YRLOG_INFO("explorer-trace|shutdown down observer({})", name_);
    if (running_.exchange(false)) {
        observeContext_->TryCancel();
    }

    observeThread_->join();
    YRLOG_INFO("explorer-trace|success to shutdown down observer({})", name_);
}

void EtcdObserver::Shutdown() noexcept
{
    Stop();
}

}  // namespace functionsystem