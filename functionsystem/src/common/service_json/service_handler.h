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

#ifndef COMMON_SERVICE_JSON_HANDLER_H
#define COMMON_SERVICE_JSON_HANDLER_H

#include <map>
#include <memory>
#include <string>

#include "common/logs/logging.h"
#include "common/service_json/service_info.h"

namespace functionsystem::service_json {

class BuildHandlerMap {
public:
    BuildHandlerMap() = default;
    virtual ~BuildHandlerMap() = default;
    virtual std::string Handler() = 0;
    virtual std::map<std::string, std::string> HookHandler(const FunctionConfig &) = 0;
    virtual std::map<std::string, std::string> ExtendedHandler(const FunctionConfig &) = 0;
    virtual std::map<std::string, int> ExtendedTimeout(const FunctionConfig &) = 0;
};

class YrLibBuilder : public BuildHandlerMap {
public:
    explicit YrLibBuilder(const std::string &runtime) : runtime_(runtime)
    {
    }
    ~YrLibBuilder() final{};
    std::string Handler() override;
    std::map<std::string, std::string> HookHandler(const FunctionConfig &) override;
    std::map<std::string, std::string> ExtendedHandler(const FunctionConfig &) override;
    std::map<std::string, int> ExtendedTimeout(const FunctionConfig &) override;

    std::string GetDefaultHandler(const std::string &handler, const std::string &defaultHandler);

private:
    std::string runtime_;
};

std::shared_ptr<BuildHandlerMap> GetBuilder(const std::string &kind, const std::string &runtime);

}  // namespace functionsystem::service_json

#endif  // COMMON_SERVICE_JSON_HANDLER_H
