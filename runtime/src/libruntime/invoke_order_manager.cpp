/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#include "invoke_order_manager.h"

namespace YR {
namespace Libruntime {
void InvokeOrderManager::CreateInstance(std::shared_ptr<InvokeSpec> spec)
{
    if (!spec->opts.needOrder || spec->returnIds.empty()) {
        return;
    }
    auto instanceId = spec->GetNamedInstanceId();
    if (instanceId.empty()) {
        instanceId = spec->returnIds[0].id;
    }
    if (instanceId.empty()) {
        return;
    }
    YRLOG_DEBUG("instanceid is {}, function meta name is {}, function meta ns is {}", instanceId,
                spec->functionMeta.name.value_or("NONE"), spec->functionMeta.ns.value_or("NONE"));

    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) == instances.end()) {
        YRLOG_DEBUG("insert instance for ordering, instance id: {}", instanceId);
        auto [it, inserted] = instances.insert({instanceId, ConstuctInstOrder()});
        (void)inserted;
        spec->invokeSeqNo = it->second->orderingCounter++;
    } else {
        YRLOG_DEBUG("insert instance for ordering, instance already exists, instance id: {}", instanceId);
    }
}

void InvokeOrderManager::CreateGroupInstance(const std::string &instanceId)
{
    if (instanceId.empty()) {
        return;
    }
    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) == instances.end()) {
        YRLOG_DEBUG("insert group instance for ordering, instance id: {}", instanceId);
        auto [it, inserted] = instances.insert({instanceId, ConstuctInstOrder()});
        (void)inserted;
        it->second->orderingCounter++;
    } else {
        YRLOG_DEBUG("insert group instance for ordering, instance already exists, instance id: {}", instanceId);
    }
}

void InvokeOrderManager::NotifyGroupInstance(const std::string &instanceId)
{
    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        auto instOrder = instances[instanceId];
        instOrder->unfinishedSeqNo++;
        YRLOG_DEBUG("current unfinished sequence No. is {}, instance id: {}", instOrder->unfinishedSeqNo, instanceId);
    }
}

void InvokeOrderManager::RemoveGroupInstance(const std::string &instanceId)
{
    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        YRLOG_DEBUG("remove group instance for ordering, instance id: {}", instanceId);
        instances.erase(instanceId);
    }
}

void InvokeOrderManager::RegisterInstance(const std::string &instanceId)
{
    if (instanceId.empty()) {
        return;
    }
    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) == instances.end()) {
        YRLOG_DEBUG("register instance for ordering, instance id: {}", instanceId);
        instances.insert({instanceId, ConstuctInstOrder()});
    } else {
        YRLOG_DEBUG("register instance for ordering, instance already exists, instance id: {}", instanceId);
    }
}

void InvokeOrderManager::RemoveInstance(std::shared_ptr<InvokeSpec> spec)
{
    if (!spec->opts.needOrder || spec->returnIds.empty()) {
        return;
    }
    auto instanceId = spec->GetNamedInstanceId();
    if (instanceId.empty()) {
        instanceId = spec->returnIds[0].id;
    }
    if (instanceId.empty()) {
        return;
    }

    YRLOG_DEBUG(
        "start Romove instanceid from order manager, id is {}, function meta name is {}, function meta ns is {}",
        instanceId, spec->functionMeta.name.value_or("NONE"), spec->functionMeta.ns.value_or("NONE"));
    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        YRLOG_DEBUG("remove instance for ordering, instance id: {}", instanceId);
        instances.erase(instanceId);
    }
}

void InvokeOrderManager::Invoke(std::shared_ptr<InvokeSpec> spec)
{
    auto instanceId = spec->GetNamedInstanceId();
    if (instanceId.empty()) {
        instanceId = spec->instanceId;
    }
    YRLOG_DEBUG("entry order manager invoke, instance id: {}, req id: {}", instanceId, spec->requestId);
    if (instanceId.empty()) {
        return;
    }

    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        auto instOrder = instances[instanceId];
        spec->invokeSeqNo = instOrder->orderingCounter++;
        YRLOG_DEBUG("instance invoke with order, instance id: {}, request id: {}, sequence No.: {}, unfinished: {}",
                    instanceId, spec->requestId, spec->invokeSeqNo, instOrder->unfinishedSeqNo);
    } else {
        if (spec->opts.isGetInstance) {
            YRLOG_DEBUG("when inovke type is get named instance, need insert instance for ordering, instance id: {}",
                        instanceId);
            auto [it, inserted] = instances.insert({instanceId, ConstuctInstOrder()});
            (void)inserted;
            spec->invokeSeqNo = it->second->orderingCounter++;
        }
    }
}

void InvokeOrderManager::UpdateUnfinishedSeq(std::shared_ptr<InvokeSpec> spec)
{
    auto instanceId = spec->GetNamedInstanceId();
    if (instanceId.empty()) {
        instanceId = spec->instanceId;
    }
    YRLOG_DEBUG("entry order manager updateUnfinishedSeq, instance id: {}, req id: {}", instanceId, spec->requestId);
    if (instanceId.empty()) {
        return;
    }

    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        auto instOrder = instances[instanceId];
        spec->invokeUnfinishedSeqNo = instOrder->unfinishedSeqNo;
        YRLOG_DEBUG(
            "instance update unfinishedSeq with order, instance id: {}, request id: {}, sequence No.: {}, "
            "unfinished No.: {}",
            instanceId, spec->requestId, spec->invokeSeqNo, instOrder->unfinishedSeqNo);
    }
}

void InvokeOrderManager::ClearInsOrderMsg(const std::string &insId, int signal)
{
    absl::MutexLock lock(&mu);
    if (signal == libruntime::Signal::KillAllInstances) {
        YRLOG_DEBUG("reveive signal kill all instances, remove all instance for ordering");
        instances.clear();
        return;
    }

    if (signal == libruntime::Signal::KillInstance || signal == libruntime::Signal::KillGroupInstance ||
        signal == libruntime::Signal::killInstanceSync) {
        if (insId.empty()) {
            return;
        }
        if (instances.find(insId) != instances.end()) {
            YRLOG_DEBUG("remove instance for ordering, signal is {}, instance id: {}", signal, insId);
            instances.erase(insId);
        }
    }
}

void InvokeOrderManager::NotifyInvokeSuccess(std::shared_ptr<InvokeSpec> spec)
{
    auto instanceId = spec->GetNamedInstanceId();
    if (instanceId.empty()) {
        if (spec->invokeType == libruntime::InvokeType::CreateInstance) {
            // create spec use return id as instance id
            instanceId = spec->returnIds[0].id;
        } else {
            instanceId = spec->instanceId;
        }
    }
    YRLOG_DEBUG("entry notify order manager invoke success, instance id: {}, req id: {}", instanceId, spec->requestId);
    if (instanceId.empty()) {
        return;
    }

    absl::MutexLock lock(&mu);
    if (instances.find(instanceId) != instances.end()) {
        auto instOrder = instances[instanceId];
        instOrder->finishedUnorderedInvokeSpecs.insert({spec->invokeSeqNo, spec});
        auto it = instOrder->finishedUnorderedInvokeSpecs.begin();
        while (it != instOrder->finishedUnorderedInvokeSpecs.end()) {
            if (it->first == instOrder->unfinishedSeqNo) {
                instOrder->unfinishedSeqNo++;
                it = instOrder->finishedUnorderedInvokeSpecs.erase(it);
            } else {
                break;
            }
        }
        YRLOG_DEBUG("current unfinished sequence No. is {}, instance id: {}, finished unordered spec size: {}",
                    instOrder->unfinishedSeqNo, instanceId, instOrder->finishedUnorderedInvokeSpecs.size());
    }
}

std::shared_ptr<InstanceOrdering> InvokeOrderManager::ConstuctInstOrder()
{
    return std::make_shared<InstanceOrdering>();
}

}  // namespace Libruntime
}  // namespace YR