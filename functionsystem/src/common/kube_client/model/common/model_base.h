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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_BASE_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_BASE_H

#include <map>
#include <vector>

#include "nlohmann/json.hpp"

#include "common/kube_client/api/api_exception.h"
#include "model_utils.h"

namespace functionsystem::kube_client {
namespace model {

class ModelBase {
public:
    ModelBase();
    virtual ~ModelBase();

    virtual nlohmann::json ToJson() const = 0;
    virtual bool FromJson(const nlohmann::json &json) = 0;
    virtual bool IsSet() const;

protected:
    bool m_isSet;
};

using CreateModelFunc = std::function<std::shared_ptr<ModelBase>()>;

class ModelFactory {
public:
    void RegisterCreateModelFunc(const std::string &modelName, const CreateModelFunc &func);
    std::shared_ptr<ModelBase> CreateModel(const std::string &modelName);
    static ModelFactory &GetInstance()
    {
        static ModelFactory instance;
        return instance;
    }
    virtual ~ModelFactory()
    {
    }
    ModelFactory(const ModelFactory &) = delete;
    ModelFactory &operator=(const ModelFactory &) = delete;

protected:
    ModelFactory()
    {
    }

private:
    std::unordered_map<std::string, CreateModelFunc> m_createFuncs;
};

std::string ToModelName(const std::string &kind);

#define REGISTER_MODEL_OBJECT(name)                                                                                \
    struct Register##name##Object {                                                                                \
        Register##name##Object() noexcept                                                                          \
        {                                                                                                          \
            ModelFactory::GetInstance().RegisterCreateModelFunc(#name, []() { return std::make_shared<name>(); }); \
        }                                                                                                          \
    };                                                                                                             \
    Register##name##Object g_Register##name##Helper

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_MODEL_BASE_H */
