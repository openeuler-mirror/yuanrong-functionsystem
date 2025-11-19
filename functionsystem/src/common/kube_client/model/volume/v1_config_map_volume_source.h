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

/*
 * V1ConfigMapVolumeSource.h
 *
 * Adapts a ConfigMap into a volume.  The contents of the target ConfigMap&#39;s Data field will be presented in a
 * volume as files using the keys in the Data field as the file names, unless the items element is populated with
 * specific mappings of keys to paths. ConfigMap volumes support ownership management and SELinux relabeling.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONFIG_MAP_VOLUME_SOURCE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONFIG_MAP_VOLUME_SOURCE_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_key_to_path.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CONFIG_MAP_VOLUME_SOURCE = "V1ConfigMapVolumeSource";

class V1ConfigMapVolumeSource : public ModelBase {
public:
    V1ConfigMapVolumeSource();
    ~V1ConfigMapVolumeSource() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    int64_t GetDefaultMode() const;
    bool DefaultModeIsSet() const;
    void UnsetDefaultMode();
    void SetDefaultMode(int64_t value);

    std::vector<std::shared_ptr<V1KeyToPath>> &GetItems();
    bool ItemsIsSet() const;
    void UnsetItems();
    void SetItems(const std::vector<std::shared_ptr<V1KeyToPath>> &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    bool IsOptional() const;
    bool OptionalIsSet() const;
    void UnsetOptional();
    void SetOptional(bool value);

protected:
    int64_t m_defaultMode;
    bool m_defaultModeIsSet;
    std::vector<std::shared_ptr<V1KeyToPath>> m_items;
    bool m_itemsIsSet;
    std::string m_name;
    bool m_nameIsSet;
    bool m_optional;
    bool m_optionalIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONFIG_MAP_VOLUME_SOURCE_H_ */
