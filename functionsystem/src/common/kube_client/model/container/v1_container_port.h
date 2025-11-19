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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_PORT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_PORT_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CONTAINER_PORT = "V1ContainerPort";

class V1ContainerPort : public ModelBase {
public:
    V1ContainerPort();
    ~V1ContainerPort() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    int64_t GetContainerPort() const;
    bool ContainerPortIsSet() const;
    void UnsetContainerPort();
    void SetContainerPort(int64_t value);

    std::string GetHostIP() const;
    bool HostIPIsSet() const;
    void UnsetHostIP();
    void SetHostIP(const std::string &value);

    int64_t GetHostPort() const;
    bool HostPortIsSet() const;
    void UnsetHostPort();
    void SetHostPort(int64_t value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::string GetProtocol() const;
    bool ProtocolIsSet() const;
    void UnsetProtocol();

    void SetProtocol(const std::string &value);

protected:
    int64_t m_containerPort;
    bool m_containerPortIsSet;
    std::string m_hostIP;
    bool m_hostIPIsSet;
    int64_t m_hostPort;
    bool m_hostPortIsSet;
    std::string m_name;
    bool m_nameIsSet;
    std::string m_protocol;
    bool m_protocolIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_PORT_H_ */
