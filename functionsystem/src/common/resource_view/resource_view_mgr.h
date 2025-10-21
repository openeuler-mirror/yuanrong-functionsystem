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

#ifndef COMMON_RESOURCE_VIEW_RESOURCE_VIEW_MGR_H
#define COMMON_RESOURCE_VIEW_RESOURCE_VIEW_MGR_H
#include "resource_view.h"
namespace functionsystem::resource_view {

enum class ResourceType {
    PRIMARY = 0,
    VIRTUAL = 1,
};

class ResourceViewMgr {
public:
    ResourceViewMgr() = default;
    virtual ~ResourceViewMgr() = default;
    void Init(const std::string &id, const ResourceViewActor::Param &param = VIEW_ACTOR_DEFAULT_PARAM);
    // never return nullptr
    virtual std::shared_ptr<ResourceView> GetInf(const ResourceType &type = ResourceType::PRIMARY);
    virtual litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>>> GetResources();
    virtual litebus::Future<std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>>> GetChanges();
    virtual void RegisterResourceUnit(const messages::Register &registerMsg, const std::string &url);
    virtual void UnRegisterResourceUnit(const std::string &id);
    virtual void UpdateDomainUrlForLocal(const std::string &addr);
    virtual void TriggerTryPull();

private:
    std::shared_ptr<ResourceView> primary_;
    std::shared_ptr<ResourceView> virtual_;
};

ResourceType GetResourceType(const InstanceInfo &info);
ResourceType GetResourceType(const messages::GroupInfo &info);
}  // namespace functionsystem::resource_view
#endif  // COMMON_RESOURCE_VIEW_RESOURCE_VIEW_MGR_H
