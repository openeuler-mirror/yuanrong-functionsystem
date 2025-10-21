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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_CLIENT_H
#define COMMON_META_STORE_CLIENT_ELECTION_CLIENT_H

#include "async/future.hpp"
#include "meta_store_client/election/observer.h"
#include "meta_store_struct.h"

namespace functionsystem::meta_store {
class ElectionClient {
public:
    virtual ~ElectionClient() = default;

    /**
     * campaign for leader
     *
     * @param name the election's identifier for the campaign
     * @param lease the ID of the lease attached to leadership of the election
     * @param value the initial proclaimed value set when the campaigner wins the election
     * @return a LeaderKey representing the leadership if successful
     */
    virtual litebus::Future<CampaignResponse> Campaign(const std::string &name, int64_t lease,
                                                       const std::string &value) = 0;

    /**
     * get the current election proclamation
     *
     * @param name the election identifier for the leadership information
     * @return the current election proclamation
     */
    virtual litebus::Future<LeaderResponse> Leader(const std::string &name) = 0;

    /**
     * releases election leadership so other campaigners may acquire leadership on the election
     *
     * @param leader the leadership to relinquish by resignation
     * @return resign result
     */
    virtual litebus::Future<ResignResponse> Resign(const LeaderKey &leader) = 0;

    /**
     * observe streams election proclamations in-order as made by the election's elected leaders
     *
     * @param name the election identifier for the leadership information
     * @param callback consumer for leader change events
     * @return the observer to stop observe
     */
    virtual litebus::Future<std::shared_ptr<Observer>> Observe(const std::string &name,
                                                               const std::function<void(LeaderResponse)> &callback) = 0;

protected:
    ElectionClient() = default;
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_ELECTION_CLIENT_H
