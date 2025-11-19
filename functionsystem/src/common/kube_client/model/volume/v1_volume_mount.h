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
 * V1VolumeMount.h
 *
 * VolumeMount describes a mounting of a Volume within a container.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_MOUNT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_MOUNT_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_VOLUME_MOUNT = "V1VolumeMount";

class V1VolumeMount : public ModelBase {
public:
    V1VolumeMount();
    ~V1VolumeMount() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetMountPath() const;
    bool MountPathIsSet() const;
    void UnsetMountPath();
    void SetMountPath(const std::string &value);

    std::string GetMountPropagation() const;
    bool MountPropagationIsSet() const;
    void UnsetMountPropagation();
    void SetMountPropagation(const std::string &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    bool IsReadOnly() const;
    bool ReadOnlyIsSet() const;
    void UnsetReadOnly();
    void SetReadOnly(bool value);

    std::string GetSubPath() const;
    bool SubPathIsSet() const;
    void UnsetSubPath();
    void SetSubPath(const std::string &value);

    std::string GetSubPathExpr() const;
    bool SubPathExprIsSet() const;
    void UnsetSubPathExpr();
    void SetSubPathExpr(const std::string &value);

protected:
    std::string m_mountPath;
    bool m_mountPathIsSet;
    std::string m_mountPropagation;
    bool m_mountPropagationIsSet;
    std::string m_name;
    bool m_nameIsSet;
    bool m_readOnly;
    bool m_readOnlyIsSet;
    std::string m_subPath;
    bool m_subPathIsSet;
    std::string m_subPathExpr;
    bool m_subPathExprIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_MOUNT_H_ */
