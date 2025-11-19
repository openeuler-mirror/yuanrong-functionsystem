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

#include "v1_probe.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Probe);

V1Probe::V1Probe()
{
    m_failureThreshold = 0;
    m_failureThresholdIsSet = false;
    m_initialDelaySeconds = 0;
    m_initialDelaySecondsIsSet = false;
    m_periodSeconds = 0;
    m_periodSecondsIsSet = false;
    m_successThreshold = 0;
    m_successThresholdIsSet = false;
    m_tcpSocketIsSet = false;
    m_terminationGracePeriodSeconds = 0L;
    m_terminationGracePeriodSecondsIsSet = false;
    m_timeoutSeconds = 0;
    m_timeoutSecondsIsSet = false;
    m_execIsSet = false;
}

V1Probe::~V1Probe()
{
}

nlohmann::json V1Probe::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_failureThresholdIsSet) {
        val["failureThreshold"] = ModelUtils::ToJson(m_failureThreshold);
    }
    if (m_initialDelaySecondsIsSet) {
        val["initialDelaySeconds"] = ModelUtils::ToJson(m_initialDelaySeconds);
    }
    if (m_periodSecondsIsSet) {
        val["periodSeconds"] = ModelUtils::ToJson(m_periodSeconds);
    }
    if (m_successThresholdIsSet) {
        val["successThreshold"] = ModelUtils::ToJson(m_successThreshold);
    }
    if (m_tcpSocketIsSet) {
        val["tcpSocket"] = ModelUtils::ToJson(m_tcpSocket);
    }
    if (m_execIsSet) {
        val["exec"] = ModelUtils::ToJson(m_exec);
    }
    if (m_terminationGracePeriodSecondsIsSet) {
        val["terminationGracePeriodSeconds"] = ModelUtils::ToJson(m_terminationGracePeriodSeconds);
    }
    if (m_timeoutSecondsIsSet) {
        val["timeoutSeconds"] = ModelUtils::ToJson(m_timeoutSeconds);
    }

    return val;
}

bool V1Probe::FromJson(const nlohmann::json &val)
{
    bool ok = ParseThresholdFromJson(val);

    if (val.contains("initialDelaySeconds")) {
        const nlohmann::json &fieldValue = val.at("initialDelaySeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetInitialDelaySeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetInitialDelaySeconds);
            SetInitialDelaySeconds(refValSetInitialDelaySeconds);
        }
    }
    if (val.contains("periodSeconds")) {
        const nlohmann::json &fieldValue = val.at("periodSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetPeriodSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPeriodSeconds);
            SetPeriodSeconds(refValSetPeriodSeconds);
        }
    }
    if (val.contains("tcpSocket")) {
        const nlohmann::json &fieldValue = val.at("tcpSocket");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1TCPSocketAction> refValSetTcpSocket;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTcpSocket);
            SetTcpSocket(refValSetTcpSocket);
        }
    }
    if (val.contains("exec")) {
        const nlohmann::json &fieldValue = val.at("exec");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ExecAction> refValSetExec;
            ok &= ModelUtils::FromJson(fieldValue, refValSetExec);
            SetExec(refValSetExec);
        }
    }
    if (val.contains("terminationGracePeriodSeconds")) {
        const nlohmann::json &fieldValue = val.at("terminationGracePeriodSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetTerminationGracePeriodSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTerminationGracePeriodSeconds);
            SetTerminationGracePeriodSeconds(refValSetTerminationGracePeriodSeconds);
        }
    }
    if (val.contains("timeoutSeconds")) {
        const nlohmann::json &fieldValue = val.at("timeoutSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetTimeoutSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTimeoutSeconds);
            SetTimeoutSeconds(refValSetTimeoutSeconds);
        }
    }
    return ok;
}

bool V1Probe::ParseThresholdFromJson(const nlohmann::json &json)
{
    bool ok = true;
    if (json.contains("failureThreshold")) {
        const nlohmann::json &fieldValue = json.at("failureThreshold");
        if (!fieldValue.is_null()) {
            int64_t refValSetFailureThreshold;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFailureThreshold);
            SetFailureThreshold(refValSetFailureThreshold);
        }
    }
    if (json.contains("successThreshold")) {
        const nlohmann::json &fieldValue = json.at("successThreshold");
        if (!fieldValue.is_null()) {
            int64_t refValSetSuccessThreshold;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSuccessThreshold);
            SetSuccessThreshold(refValSetSuccessThreshold);
        }
    }
    return ok;
}

int64_t V1Probe::GetFailureThreshold() const
{
    return m_failureThreshold;
}

void V1Probe::SetFailureThreshold(int64_t value)
{
    m_failureThreshold = value;
    m_failureThresholdIsSet = true;
}

bool V1Probe::FailureThresholdIsSet() const
{
    return m_failureThresholdIsSet;
}

void V1Probe::UnsetFailureThreshold()
{
    m_failureThresholdIsSet = false;
}
int64_t V1Probe::GetInitialDelaySeconds() const
{
    return m_initialDelaySeconds;
}

void V1Probe::SetInitialDelaySeconds(int64_t value)
{
    m_initialDelaySeconds = value;
    m_initialDelaySecondsIsSet = true;
}

bool V1Probe::InitialDelaySecondsIsSet() const
{
    return m_initialDelaySecondsIsSet;
}

void V1Probe::UnsetInitialDelaySeconds()
{
    m_initialDelaySecondsIsSet = false;
}
int64_t V1Probe::GetPeriodSeconds() const
{
    return m_periodSeconds;
}

void V1Probe::SetPeriodSeconds(int64_t value)
{
    m_periodSeconds = value;
    m_periodSecondsIsSet = true;
}

bool V1Probe::PeriodSecondsIsSet() const
{
    return m_periodSecondsIsSet;
}

void V1Probe::UnsetPeriodSeconds()
{
    m_periodSecondsIsSet = false;
}
int64_t V1Probe::GetSuccessThreshold() const
{
    return m_successThreshold;
}

void V1Probe::SetSuccessThreshold(int64_t value)
{
    m_successThreshold = value;
    m_successThresholdIsSet = true;
}

bool V1Probe::SuccessThresholdIsSet() const
{
    return m_successThresholdIsSet;
}

void V1Probe::UnsetSuccessThreshold()
{
    m_successThresholdIsSet = false;
}

std::shared_ptr<V1TCPSocketAction> V1Probe::GetTcpSocket() const
{
    return m_tcpSocket;
}

void V1Probe::SetTcpSocket(const std::shared_ptr<V1TCPSocketAction> &value)
{
    m_tcpSocket = value;
    m_tcpSocketIsSet = true;
}

bool V1Probe::TcpSocketIsSet() const
{
    return m_tcpSocketIsSet;
}

void V1Probe::UnsetTcpSocket()
{
    m_tcpSocketIsSet = false;
}

std::shared_ptr<V1ExecAction> V1Probe::GetExec() const
{
    return m_exec;
}

void V1Probe::SetExec(const std::shared_ptr<V1ExecAction> &value)
{
    m_exec = value;
    m_execIsSet = true;
}

bool V1Probe::ExecIsSet() const
{
    return m_execIsSet;
}

void V1Probe::UnsetExec()
{
    m_execIsSet = false;
}

int64_t V1Probe::GetTerminationGracePeriodSeconds() const
{
    return m_terminationGracePeriodSeconds;
}

void V1Probe::SetTerminationGracePeriodSeconds(int64_t value)
{
    m_terminationGracePeriodSeconds = value;
    m_terminationGracePeriodSecondsIsSet = true;
}

bool V1Probe::TerminationGracePeriodSecondsIsSet() const
{
    return m_terminationGracePeriodSecondsIsSet;
}

void V1Probe::UnsetTerminationGracePeriodSeconds()
{
    m_terminationGracePeriodSecondsIsSet = false;
}
int64_t V1Probe::GetTimeoutSeconds() const
{
    return m_timeoutSeconds;
}

void V1Probe::SetTimeoutSeconds(int64_t value)
{
    m_timeoutSeconds = value;
    m_timeoutSecondsIsSet = true;
}

bool V1Probe::TimeoutSecondsIsSet() const
{
    return m_timeoutSecondsIsSet;
}

void V1Probe::UnsetTimeoutSeconds()
{
    m_timeoutSecondsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
