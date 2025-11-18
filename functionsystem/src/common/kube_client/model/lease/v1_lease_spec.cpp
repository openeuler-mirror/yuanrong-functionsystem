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

#include "v1_lease_spec.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1LeaseSpec);

V1LeaseSpec::V1LeaseSpec()
{
    m_acquireTime = utility::Datetime();
    m_acquireTimeIsSet = false;
    m_holderIdentity = std::string("");
    m_holderIdentityIsSet = false;
    m_leaseDurationSeconds = 0;
    m_leaseDurationSecondsIsSet = false;
    m_leaseTransitions = 0;
    m_leaseTransitionsIsSet = false;
    m_renewTime = utility::Datetime();
    m_renewTimeIsSet = false;
}

V1LeaseSpec::~V1LeaseSpec()
{
}

nlohmann::json V1LeaseSpec::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_acquireTimeIsSet) {
        val["acquireTime"] = ModelUtils::ToJson(m_acquireTime);
    }
    if (m_holderIdentityIsSet) {
        val["holderIdentity"] = ModelUtils::ToJson(m_holderIdentity);
    }
    if (m_leaseDurationSecondsIsSet) {
        val["leaseDurationSeconds"] = ModelUtils::ToJson(m_leaseDurationSeconds);
    }
    if (m_leaseTransitionsIsSet) {
        val["leaseTransitions"] = ModelUtils::ToJson(m_leaseTransitions);
    }
    if (m_renewTimeIsSet) {
        val["renewTime"] = ModelUtils::ToJson(m_renewTime);
    }
    return val;
}

bool V1LeaseSpec::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("acquireTime")) {
        const nlohmann::json &fieldValue = val.at("acquireTime");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetAcquireTime;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAcquireTime);
            SetAcquireTime(refValSetAcquireTime);
        }
    }
    if (val.contains("holderIdentity")) {
        const nlohmann::json &fieldValue = val.at("holderIdentity");
        if (!fieldValue.is_null()) {
            std::string refValSetHolderIdentity;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHolderIdentity);
            SetHolderIdentity(refValSetHolderIdentity);
        }
    }
    if (val.contains("leaseDurationSeconds")) {
        const nlohmann::json &fieldValue = val.at("leaseDurationSeconds");
        if (!fieldValue.is_null()) {
            int32_t refValSetLeaseDurationSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLeaseDurationSeconds);
            SetLeaseDurationSeconds(refValSetLeaseDurationSeconds);
        }
    }
    if (val.contains("leaseTransitions")) {
        const nlohmann::json &fieldValue = val.at("leaseTransitions");
        if (!fieldValue.is_null()) {
            int32_t refValSetLeaseTransitions;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLeaseTransitions);
            SetLeaseTransitions(refValSetLeaseTransitions);
        }
    }
    if (val.contains("renewTime")) {
        const nlohmann::json &fieldValue = val.at("renewTime");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetRenewTime;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRenewTime);
            SetRenewTime(refValSetRenewTime);
        }
    }
    return ok;
}

utility::Datetime V1LeaseSpec::GetAcquireTime() const
{
    return m_acquireTime;
}

void V1LeaseSpec::SetAcquireTime(const utility::Datetime &value)
{
    m_acquireTime = value;
    m_acquireTimeIsSet = true;
}

bool V1LeaseSpec::AcquireTimeIsSet() const
{
    return m_acquireTimeIsSet;
}

void V1LeaseSpec::UnsetAcquireTime()
{
    m_acquireTimeIsSet = false;
}
std::string V1LeaseSpec::GetHolderIdentity() const
{
    return m_holderIdentity;
}

void V1LeaseSpec::SetHolderIdentity(const std::string &value)
{
    m_holderIdentity = value;
    m_holderIdentityIsSet = true;
}

bool V1LeaseSpec::HolderIdentityIsSet() const
{
    return m_holderIdentityIsSet;
}

void V1LeaseSpec::UnsetHolderIdentity()
{
    m_holderIdentityIsSet = false;
}
int32_t V1LeaseSpec::GetLeaseDurationSeconds() const
{
    return m_leaseDurationSeconds;
}

void V1LeaseSpec::SetLeaseDurationSeconds(int32_t value)
{
    m_leaseDurationSeconds = value;
    m_leaseDurationSecondsIsSet = true;
}

bool V1LeaseSpec::LeaseDurationSecondsIsSet() const
{
    return m_leaseDurationSecondsIsSet;
}

void V1LeaseSpec::UnsetLeaseDurationSeconds()
{
    m_leaseDurationSecondsIsSet = false;
}
int32_t V1LeaseSpec::GetLeaseTransitions() const
{
    return m_leaseTransitions;
}

void V1LeaseSpec::SetLeaseTransitions(int32_t value)
{
    m_leaseTransitions = value;
    m_leaseTransitionsIsSet = true;
}

bool V1LeaseSpec::LeaseTransitionsIsSet() const
{
    return m_leaseTransitionsIsSet;
}

void V1LeaseSpec::UnsetLeaseTransitions()
{
    m_leaseTransitionsIsSet = false;
}

utility::Datetime V1LeaseSpec::GetRenewTime() const
{
    return m_renewTime;
}

void V1LeaseSpec::SetRenewTime(const utility::Datetime &value)
{
    m_renewTime = value;
    m_renewTimeIsSet = true;
}

bool V1LeaseSpec::RenewTimeIsSet() const
{
    return m_renewTimeIsSet;
}

void V1LeaseSpec::UnsetRenewTime()
{
    m_renewTimeIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client