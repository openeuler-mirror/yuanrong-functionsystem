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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_META_STORE_OBSERVER_H
#define COMMON_META_STORE_CLIENT_ELECTION_META_STORE_OBSERVER_H

#include "meta_store_client/meta_store_struct.h"
#include "observer.h"

namespace functionsystem {

class MetaStoreObserver : public Observer {
public:
    MetaStoreObserver(std::string name, std::function<void(LeaderResponse)> callback,
                      const std::string &etcdTablePrefix,
                      const std::function<void(uint64_t)> &closeMethod);

    ~MetaStoreObserver() override = default;

    void Shutdown() noexcept override;

    void OnObserve(const LeaderResponse &leaderResponse);

    uint64_t GetObserveID() const;
    void SetObserveID(uint64_t observeID);

    std::string GetName() const;
    bool IsCanceled() const;
    std::function<void(LeaderResponse)> GetCallBack() const;

private:
    uint64_t observeID_;
    bool isCanceled_ = false;
    std::function<void(uint64_t)> closeMethod_;
};

}  // namespace functionsystem

#endif  // COMMON_META_STORE_CLIENT_ELECTION_META_STORE_OBSERVER_H
