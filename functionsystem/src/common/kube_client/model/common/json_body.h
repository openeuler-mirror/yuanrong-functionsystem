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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_JSON_BODY_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_JSON_BODY_H_

#include "nlohmann/json.hpp"

#include "ihttp_body.h"

namespace functionsystem::kube_client {
namespace model {

class JsonBody : public IHttpBody {
public:
    explicit JsonBody(const nlohmann::json &value);
    ~JsonBody() override;

    void WriteTo(std::ostream &target) override;

protected:
    nlohmann::json m_json;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_JsonBody_H_ */
