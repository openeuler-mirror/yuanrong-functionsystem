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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_PROBE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_PROBE_H_

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_exec_action.h"
#include "v1_tcp_socket_action.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_PROBE = "V1Probe";

class V1Probe : public ModelBase {
public:
    V1Probe();
    ~V1Probe() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseThresholdFromJson(const nlohmann::json &json);

    int64_t GetFailureThreshold() const;
    bool FailureThresholdIsSet() const;
    void UnsetFailureThreshold();
    void SetFailureThreshold(int64_t value);

    int64_t GetInitialDelaySeconds() const;
    bool InitialDelaySecondsIsSet() const;
    void UnsetInitialDelaySeconds();
    void SetInitialDelaySeconds(int64_t value);

    int64_t GetPeriodSeconds() const;
    bool PeriodSecondsIsSet() const;
    void UnsetPeriodSeconds();
    void SetPeriodSeconds(int64_t value);

    int64_t GetSuccessThreshold() const;
    bool SuccessThresholdIsSet() const;
    void UnsetSuccessThreshold();
    void SetSuccessThreshold(int64_t value);

    std::shared_ptr<V1TCPSocketAction> GetTcpSocket() const;
    bool TcpSocketIsSet() const;
    void UnsetTcpSocket();
    void SetTcpSocket(const std::shared_ptr<V1TCPSocketAction> &value);

    std::shared_ptr<V1ExecAction> GetExec() const;
    bool ExecIsSet() const;
    void UnsetExec();
    void SetExec(const std::shared_ptr<V1ExecAction> &value);

    int64_t GetTerminationGracePeriodSeconds() const;
    bool TerminationGracePeriodSecondsIsSet() const;
    void UnsetTerminationGracePeriodSeconds();
    void SetTerminationGracePeriodSeconds(int64_t value);

    int64_t GetTimeoutSeconds() const;
    bool TimeoutSecondsIsSet() const;
    void UnsetTimeoutSeconds();
    void SetTimeoutSeconds(int64_t value);

protected:
    int64_t m_failureThreshold;
    bool m_failureThresholdIsSet;
    int64_t m_initialDelaySeconds;
    bool m_initialDelaySecondsIsSet;
    int64_t m_periodSeconds;
    bool m_periodSecondsIsSet;
    int64_t m_successThreshold;
    bool m_successThresholdIsSet;
    std::shared_ptr<V1TCPSocketAction> m_tcpSocket;
    bool m_tcpSocketIsSet;
    std::shared_ptr<V1ExecAction> m_exec;
    bool m_execIsSet;
    int64_t m_terminationGracePeriodSeconds;
    bool m_terminationGracePeriodSecondsIsSet;
    int64_t m_timeoutSeconds;
    bool m_timeoutSecondsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_PROBE_H_ */
