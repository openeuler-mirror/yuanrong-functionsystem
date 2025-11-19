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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_SPEC_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_SPEC_H

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_label_selector.h"
#include "common/kube_client/model/pod/v1_pod_template_spec.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_DEPLOYMENT_SPEC = "V1DeploymentSpec";

class V1DeploymentSpec : public ModelBase {
public:
    V1DeploymentSpec();
    ~V1DeploymentSpec() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseBaseFromJson(const nlohmann::json &json);

    int64_t GetMinReadySeconds() const;
    bool MinReadySecondsIsSet() const;
    void UnsetMinReadySeconds();
    void SetMinReadySeconds(int64_t value);

    bool IsPaused() const;
    bool PausedIsSet() const;
    void UnsetPaused();
    void SetPaused(bool value);

    int64_t GetProgressDeadlineSeconds() const;
    bool ProgressDeadlineSecondsIsSet() const;
    void UnsetProgressDeadlineSeconds();
    void SetProgressDeadlineSeconds(int64_t value);

    int64_t GetReplicas() const;
    bool ReplicasIsSet() const;
    void UnsetReplicas();
    void SetReplicas(int64_t value);

    int64_t GetRevisionHistoryLimit() const;
    bool RevisionHistoryLimitIsSet() const;
    void UnsetRevisionHistoryLimit();
    void SetRevisionHistoryLimit(int64_t value);

    std::shared_ptr<V1LabelSelector> GetSelector() const;
    bool SelectorIsSet() const;
    void UnSetSelector();
    void SetSelector(const std::shared_ptr<V1LabelSelector> &value);

    std::shared_ptr<V1PodTemplateSpec> GetRTemplate() const;
    bool RTemplateIsSet() const;
    void UnsetRTemplate();
    void SetRTemplate(const std::shared_ptr<V1PodTemplateSpec> &value);

protected:
    int64_t m_minReadySeconds;
    bool m_minReadySecondsIsSet;
    bool m_paused;
    bool m_pausedIsSet;
    int64_t m_progressDeadlineSeconds;
    bool m_progressDeadlineSecondsIsSet;
    int64_t m_replicas;
    bool m_replicasIsSet;
    int64_t m_revisionHistoryLimit;
    bool m_revisionHistoryLimitIsSet;
    std::shared_ptr<V1LabelSelector> m_selector;
    bool m_selectorIsSet;
    std::shared_ptr<V1PodTemplateSpec> m_rTemplate;
    bool m_rTemplateIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_SPEC_H */
