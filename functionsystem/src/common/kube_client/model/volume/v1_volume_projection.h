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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_PROJECTION_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_PROJECTION_H_

#include "common/kube_client/model/common/model_base.h"
#include "v1_downward_api_projection.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_VOLUME_PROJECTION = "V1VolumeProjection";

class V1VolumeProjection : public ModelBase {
public:
    V1VolumeProjection();
    ~V1VolumeProjection() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1DownwardAPIProjection> GetDownwardAPI() const;
    bool DownwardAPIIsSet() const;
    void UnsetDownwardAPI();
    void SetDownwardAPI(const std::shared_ptr<V1DownwardAPIProjection> &value);

protected:
    std::shared_ptr<V1DownwardAPIProjection> m_downwardAPI;
    bool m_downwardAPIIsSet;
};
}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* ORG_OPENAPITOOLS_CLIENT_MODEL_V1_VolumeProjection_H_ */
