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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TCP_SOCKET_ACTION_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TCP_SOCKET_ACTION_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_TCP_SOCKET_ACTION = "V1TCPSocketAction";

class V1TCPSocketAction : public ModelBase {
public:
    V1TCPSocketAction();
    ~V1TCPSocketAction() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetHost() const;
    bool HostIsSet() const;
    void UnsetHost();
    void SetHost(const std::string &value);

    int64_t GetPort() const;
    bool PortIsSet() const;
    void UnsetPort();
    void SetPort(int64_t value);

protected:
    std::string m_host;
    bool m_hostIsSet;
    int64_t m_port;
    bool m_portIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TCP_SOCKET_ACTION_H_ */
