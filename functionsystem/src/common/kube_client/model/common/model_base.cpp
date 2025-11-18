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

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {
const std::string VERSION_STR = "V1";

ModelBase::ModelBase() : m_isSet(false)
{
}
ModelBase::~ModelBase()
{
}
bool ModelBase::IsSet() const
{
    return m_isSet;
}

void ModelFactory::RegisterCreateModelFunc(const std::string &modelName, const CreateModelFunc &func)
{
    auto iter = m_createFuncs.find(modelName);
    if (iter != m_createFuncs.end()) {
        return;
    }
    m_createFuncs[modelName] = func;
}

std::shared_ptr<ModelBase> ModelFactory::CreateModel(const std::string &modelName)
{
    auto iter = m_createFuncs.find(modelName);
    if (iter != m_createFuncs.end()) {
        return iter->second();
    }

    return nullptr;
}

std::string ToModelName(const std::string &kind)
{
    return VERSION_STR + kind;
}
}  // namespace model
}  // namespace functionsystem::kube_client