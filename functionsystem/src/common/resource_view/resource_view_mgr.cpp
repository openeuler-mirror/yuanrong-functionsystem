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
#include "resource_view_mgr.h"
#include "common/constants/constants.h"

#include "async/defer.hpp"
#include "async/collect.hpp"
#include "common/logs/logging.h"

namespace functionsystem::resource_view {
using ResourceUnitPair = std::pair<ResourceType, std::shared_ptr<ResourceUnit>>;
using ResourceUnitChangesPair = std::pair<ResourceType, std::shared_ptr<ResourceUnitChanges>>;

ResourceType ParseResourceTag(const std::string &tag)
{
    if (!tag.empty() && tag != PRIMARY_TAG) {
        return ResourceType::VIRTUAL;
    }
    return ResourceType::PRIMARY;
}

ResourceType GetResourceType(const InstanceInfo &info)
{
    const auto &rgroup = info.scheduleoption().rgroupname();
    return ParseResourceTag(rgroup);
}

ResourceType GetResourceType(const messages::GroupInfo &info)
{
    const auto &rgroup = info.rgroupname();
    return ParseResourceTag(rgroup);
}

void ResourceViewMgr::Init(const std::string &id, const ResourceViewActor::Param &param)
{
    primary_ = ResourceView::CreateResourceView(id, param);
    auto virtualParam = param;
    virtualParam.enableTenantAffinity = false;
    virtual_ = ResourceView::CreateResourceView(id, virtualParam, VIRTUAL_TAG);
}

std::shared_ptr<ResourceView> ResourceViewMgr::GetInf(const ResourceType &type)
{
    switch (type) {
        case (ResourceType::VIRTUAL) : {
            ASSERT_IF_NULL(virtual_);
            return virtual_;
        }
        case (ResourceType::PRIMARY) :
        default: {
            ASSERT_IF_NULL(primary_);
            return primary_;
        }
    }
    return primary_;
}

litebus::Future<ResourceUnitPair> GetResourceUnit(
    const std::shared_ptr<ResourceView> &inf, const ResourceType &type)
{
    auto promise = std::make_shared<litebus::Promise<std::pair<ResourceType, std::shared_ptr<ResourceUnit>>>>();
    inf->GetFullResourceView().OnComplete(
        [promise, type](const litebus::Future<std::shared_ptr<ResourceUnit>> &future) {
            if (future.IsError()) {
                promise->SetFailed(StatusCode::FAILED);
                return;
            }
            auto unit = future.Get();
            promise->SetValue(std::make_pair(type, unit));
        });
    return promise->GetFuture();
}

litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>>> ResourceViewMgr::GetResources()
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    auto promise = std::make_shared<litebus::Promise<std::unordered_map<ResourceType,
                                                                        std::shared_ptr<ResourceUnit>>>>();
    std::list<litebus::Future<ResourceUnitPair>> futures;
    futures.emplace_back(GetResourceUnit(primary_, ResourceType::PRIMARY));
    futures.emplace_back(GetResourceUnit(virtual_, ResourceType::VIRTUAL));
    litebus::Collect(futures).OnComplete(
        [promise](const litebus::Future<std::list<ResourceUnitPair>> &future) {
            auto resources = std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>>();
            if (future.IsError()) {
                promise->SetValue(resources);
                return;
            }
            auto unitPairs = future.Get();
            for (auto item : unitPairs) {
                resources[item.first] = item.second;
            }
            promise->SetValue(resources);
        });
    return promise->GetFuture();
}

litebus::Future<ResourceUnitChangesPair> GetResourceViewChanges(
    const std::shared_ptr<ResourceView> &inf, const ResourceType &type)
{
    auto promise = std::make_shared<litebus::Promise<std::pair<ResourceType, std::shared_ptr<ResourceUnitChanges>>>>();
    inf->GetResourceViewChanges().OnComplete(
        [promise, type](const litebus::Future<std::shared_ptr<ResourceUnitChanges>> &future) {
            if (future.IsError()) {
                promise->SetFailed(StatusCode::FAILED);
                return;
            }
            auto changes = future.Get();
            promise->SetValue(std::make_pair(type, changes));
        });
    return promise->GetFuture();
}

litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>>> ResourceViewMgr::GetChanges()
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    auto promise = std::make_shared<litebus::Promise<std::unordered_map<ResourceType,
                                                                        std::shared_ptr<ResourceUnitChanges>>>>();

    std::list<litebus::Future<ResourceUnitChangesPair>> futures;
    futures.emplace_back(GetResourceViewChanges(primary_, ResourceType::PRIMARY));
    futures.emplace_back(GetResourceViewChanges(virtual_, ResourceType::VIRTUAL));
    litebus::Collect(futures).OnComplete(
        [promise](const litebus::Future<std::list<ResourceUnitChangesPair>> &future) {
            auto changes = std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>>();
            if (future.IsError()) {
                promise->SetValue(changes);
                return;
            }
            auto changesPairs = future.Get();
            for (auto item : changesPairs) {
                changes[item.first] = item.second;
            }
            promise->SetValue(changes);
        });
    return promise->GetFuture();
}

void ResourceViewMgr::UpdateDomainUrlForLocal(const std::string &addr)
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    primary_->UpdateDomainUrlForLocal(addr);
    virtual_->UpdateDomainUrlForLocal(addr);
}

void ResourceViewMgr::RegisterResourceUnit(const messages::Register &registerMsg, const std::string &url)
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    for (auto [type, resource] : registerMsg.resources()) {
        if (type == static_cast<int32_t>(ResourceType::PRIMARY)) {
            primary_->AddResourceUnitWithUrl(resource, url);
        }
        if (type == static_cast<int32_t>(ResourceType::VIRTUAL)) {
            virtual_->AddResourceUnitWithUrl(resource, url);
        }
    }
}

void ResourceViewMgr::UnRegisterResourceUnit(const std::string &id)
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    primary_->DeleteLocalResourceView(id);
    virtual_->DeleteLocalResourceView(id);
}

void ResourceViewMgr::TriggerTryPull()
{
    ASSERT_IF_NULL(virtual_);
    ASSERT_IF_NULL(primary_);
    primary_->TriggerTryPull();
    virtual_->TriggerTryPull();
}
}