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
#include "meta_store_operate_cacher.h"
namespace functionsystem {

void MetaStoreOperateCacher::AddPutEvent(const std::string &prefixKey, const std::string &key,
                                         const std::string &description)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto deleteEvent = deleteEventMap_.find(prefixKey);
    if (deleteEvent == deleteEventMap_.end()) {
        putEventMap_[prefixKey][key] = description;
        return;
    }
    if (auto iter = deleteEvent->second.find(key); iter != deleteEvent->second.end()) {
        YRLOG_WARN("key({}) has been delete before, don't need add to put event");
        return;
    }
    putEventMap_[prefixKey][key] = description;  // need to override
}

void MetaStoreOperateCacher::AddDeleteEvent(const std::string &prefixKey, const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    deleteEventMap_[prefixKey].emplace(key);
}

void MetaStoreOperateCacher::ErasePutEvent(const std::string &prefixKey, const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto putEvent = putEventMap_.find(prefixKey);
    if (putEvent != putEventMap_.end()) {
        putEvent->second.erase(key);
    }
}

void MetaStoreOperateCacher::EraseDeleteEvent(const std::string &prefixKey, const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto deleteEvent = deleteEventMap_.find(prefixKey);
    if (deleteEvent != deleteEventMap_.end()) {
        deleteEvent->second.erase(key);
        return;
    }
}

bool MetaStoreOperateCacher::IsCacheClear(const std::string &prefixKey)
{
    bool ret = true;
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto putEvent = putEventMap_.find(prefixKey); putEvent != putEventMap_.end()) {
        ret = ret && putEvent->second.empty();
    }
    if (auto deleteEvent = deleteEventMap_.find(prefixKey); deleteEvent != deleteEventMap_.end()) {
        ret = ret && deleteEvent->second.empty();
    }
    return ret;
}

}  // namespace functionsystem