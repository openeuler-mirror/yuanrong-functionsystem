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

#include "v1_deployment_spec.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DeploymentSpec);

V1DeploymentSpec::V1DeploymentSpec()
{
    m_minReadySeconds = 0;
    m_minReadySecondsIsSet = false;
    m_paused = false;
    m_pausedIsSet = false;
    m_progressDeadlineSeconds = 0;
    m_progressDeadlineSecondsIsSet = false;
    m_replicas = 0;
    m_replicasIsSet = false;
    m_revisionHistoryLimit = 0;
    m_revisionHistoryLimitIsSet = false;
    m_selectorIsSet = false;
    m_rTemplateIsSet = false;
}

V1DeploymentSpec::~V1DeploymentSpec()
{
}

nlohmann::json V1DeploymentSpec::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_minReadySecondsIsSet) {
        val["minReadySeconds"] = ModelUtils::ToJson(m_minReadySeconds);
    }
    if (m_pausedIsSet) {
        val["paused"] = ModelUtils::ToJson(m_paused);
    }
    if (m_progressDeadlineSecondsIsSet) {
        val["progressDeadlineSeconds"] = ModelUtils::ToJson(m_progressDeadlineSeconds);
    }
    if (m_replicasIsSet) {
        val["replicas"] = ModelUtils::ToJson(m_replicas);
    }
    if (m_revisionHistoryLimitIsSet) {
        val["revisionHistoryLimit"] = ModelUtils::ToJson(m_revisionHistoryLimit);
    }
    if (m_selectorIsSet) {
        val["selector"] = ModelUtils::ToJson(m_selector);
    }
    if (m_rTemplateIsSet) {
        val["template"] = ModelUtils::ToJson(m_rTemplate);
    }

    return val;
}

bool V1DeploymentSpec::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val);

    if (val.contains("replicas")) {
        const nlohmann::json &fieldValue = val.at("replicas");
        if (!fieldValue.is_null()) {
            int64_t refValSetReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReplicas);
            SetReplicas(refValSetReplicas);
        }
    }
    if (val.contains("revisionHistoryLimit")) {
        const nlohmann::json &fieldValue = val.at("revisionHistoryLimit");
        if (!fieldValue.is_null()) {
            int64_t refValSetRevisionHistoryLimit;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRevisionHistoryLimit);
            SetRevisionHistoryLimit(refValSetRevisionHistoryLimit);
        }
    }
    if (val.contains("selector")) {
        const nlohmann::json &fieldValue = val.at("selector");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LabelSelector> refValSetSelector;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSelector);
            SetSelector(refValSetSelector);
        }
    }
    if (val.contains("template")) {
        const nlohmann::json &fieldValue = val.at("template");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodTemplateSpec> refValSetRTemplate;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRTemplate);
            SetRTemplate(refValSetRTemplate);
        }
    }
    return ok;
}

bool V1DeploymentSpec::ParseBaseFromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("minReadySeconds")) {
        const nlohmann::json &fieldValue = val.at("minReadySeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetMinReadySeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMinReadySeconds);
            SetMinReadySeconds(refValSetMinReadySeconds);
        }
    }
    if (val.contains("paused")) {
        const nlohmann::json &fieldValue = val.at("paused");
        if (!fieldValue.is_null()) {
            bool refValSetPaused;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPaused);
            SetPaused(refValSetPaused);
        }
    }
    if (val.contains("progressDeadlineSeconds")) {
        const nlohmann::json &fieldValue = val.at("progressDeadlineSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetProgressDeadlineSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetProgressDeadlineSeconds);
            SetProgressDeadlineSeconds(refValSetProgressDeadlineSeconds);
        }
    }
    return ok;
}

int64_t V1DeploymentSpec::GetMinReadySeconds() const
{
    return m_minReadySeconds;
}

void V1DeploymentSpec::SetMinReadySeconds(int64_t value)
{
    m_minReadySeconds = value;
    m_minReadySecondsIsSet = true;
}

bool V1DeploymentSpec::MinReadySecondsIsSet() const
{
    return m_minReadySecondsIsSet;
}

void V1DeploymentSpec::UnsetMinReadySeconds()
{
    m_minReadySecondsIsSet = false;
}
bool V1DeploymentSpec::IsPaused() const
{
    return m_paused;
}

void V1DeploymentSpec::SetPaused(bool value)
{
    m_paused = value;
    m_pausedIsSet = true;
}

bool V1DeploymentSpec::PausedIsSet() const
{
    return m_pausedIsSet;
}

void V1DeploymentSpec::UnsetPaused()
{
    m_pausedIsSet = false;
}
int64_t V1DeploymentSpec::GetProgressDeadlineSeconds() const
{
    return m_progressDeadlineSeconds;
}

void V1DeploymentSpec::SetProgressDeadlineSeconds(int64_t value)
{
    m_progressDeadlineSeconds = value;
    m_progressDeadlineSecondsIsSet = true;
}

bool V1DeploymentSpec::ProgressDeadlineSecondsIsSet() const
{
    return m_progressDeadlineSecondsIsSet;
}

void V1DeploymentSpec::UnsetProgressDeadlineSeconds()
{
    m_progressDeadlineSecondsIsSet = false;
}
int64_t V1DeploymentSpec::GetReplicas() const
{
    return m_replicas;
}

void V1DeploymentSpec::SetReplicas(int64_t value)
{
    m_replicas = value;
    m_replicasIsSet = true;
}

bool V1DeploymentSpec::ReplicasIsSet() const
{
    return m_replicasIsSet;
}

void V1DeploymentSpec::UnsetReplicas()
{
    m_replicasIsSet = false;
}
int64_t V1DeploymentSpec::GetRevisionHistoryLimit() const
{
    return m_revisionHistoryLimit;
}

void V1DeploymentSpec::SetRevisionHistoryLimit(int64_t value)
{
    m_revisionHistoryLimit = value;
    m_revisionHistoryLimitIsSet = true;
}

bool V1DeploymentSpec::RevisionHistoryLimitIsSet() const
{
    return m_revisionHistoryLimitIsSet;
}

void V1DeploymentSpec::UnsetRevisionHistoryLimit()
{
    m_revisionHistoryLimitIsSet = false;
}
std::shared_ptr<V1LabelSelector> V1DeploymentSpec::GetSelector() const
{
    return m_selector;
}

void V1DeploymentSpec::SetSelector(const std::shared_ptr<V1LabelSelector> &value)
{
    m_selector = value;
    m_selectorIsSet = true;
}

bool V1DeploymentSpec::SelectorIsSet() const
{
    return m_selectorIsSet;
}

void V1DeploymentSpec::UnSetSelector()
{
    m_selectorIsSet = false;
}
std::shared_ptr<V1PodTemplateSpec> V1DeploymentSpec::GetRTemplate() const
{
    return m_rTemplate;
}

void V1DeploymentSpec::SetRTemplate(const std::shared_ptr<V1PodTemplateSpec> &value)
{
    m_rTemplate = value;
    m_rTemplateIsSet = true;
}

bool V1DeploymentSpec::RTemplateIsSet() const
{
    return m_rTemplateIsSet;
}

void V1DeploymentSpec::UnsetRTemplate()
{
    m_rTemplateIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
