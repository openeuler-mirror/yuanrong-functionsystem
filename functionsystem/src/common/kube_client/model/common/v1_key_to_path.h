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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_KEY_TO_PATH_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_KEY_TO_PATH_H_

#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_KEY_TO_PATH = "V1KeyToPath";

class V1KeyToPath : public ModelBase {
public:
    V1KeyToPath();
    ~V1KeyToPath() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetKey() const;
    bool KeyIsSet() const;
    void UnsetKey();
    void SetKey(const std::string &value);

    int64_t GetMode() const;
    bool ModeIsSet() const;
    void UnsetMode();
    void SetMode(int64_t value);

    std::string GetPath() const;
    bool PathIsSet() const;
    void UnsetPath();
    void SetPath(const std::string &value);

protected:
    std::string m_key;
    bool m_keyIsSet;
    int64_t m_mode;
    bool m_modeIsSet;
    std::string m_path;
    bool m_pathIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_KEY_TO_PATH_H_ */
