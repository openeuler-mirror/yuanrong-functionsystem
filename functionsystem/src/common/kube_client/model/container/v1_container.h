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
 * V1Container.h
 *
 * A single application container that you want to run within a pod.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_env_from_source.h"
#include "common/kube_client/model/common/v1_env_var.h"
#include "common/kube_client/model/common/v1_lifecycle.h"
#include "common/kube_client/model/common/v1_resource_requirements.h"
#include "common/kube_client/model/common/v1_security_context.h"
#include "common/kube_client/model/volume/v1_volume_mount.h"
#include "v1_container_port.h"
#include "v1_probe.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CONTAINER = "V1Container";

class V1Container : public ModelBase {
public:
    V1Container();
    ~V1Container() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseBaseFromJson(const nlohmann::json &json);

    bool ParseProbeInfoFromJson(const nlohmann::json &json);

    std::vector<std::string> &GetArgs();
    bool ArgsIsSet() const;
    void UnsetArgs();
    void SetArgs(const std::vector<std::string> &value);

    std::vector<std::string> &GetCommand();
    bool CommandIsSet() const;
    void UnsetCommand();
    void SetCommand(const std::vector<std::string> &value);

    std::vector<std::shared_ptr<V1EnvVar>> &GetEnv();
    bool EnvIsSet() const;
    void UnsetEnv();
    void SetEnv(const std::vector<std::shared_ptr<V1EnvVar>> &value);

    std::vector<std::shared_ptr<V1EnvFromSource>>& GetEnvFrom();
    bool EnvFromIsSet() const;
    void UnsetEnvFrom();
    void SetEnvFrom(const std::vector<std::shared_ptr<V1EnvFromSource>>& value);

    std::string GetImage() const;
    bool ImageIsSet() const;
    void UnsetImage();
    void SetImage(const std::string &value);

    std::shared_ptr<V1Lifecycle> GetLifecycle() const;
    bool LifecycleIsSet() const;
    void UnsetLifecycle();
    void SetLifecycle(const std::shared_ptr<V1Lifecycle> &value);

    std::shared_ptr<V1Probe> GetLivenessProbe() const;
    bool LivenessProbeIsSet() const;
    void UnsetLivenessProbe();
    void SetLivenessProbe(const std::shared_ptr<V1Probe> &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::vector<std::shared_ptr<V1ContainerPort>> &GetPorts();
    bool PortsIsSet() const;
    void UnsetPorts();
    void SetPorts(const std::vector<std::shared_ptr<V1ContainerPort>> &value);

    std::shared_ptr<V1Probe> GetReadinessProbe() const;
    bool ReadinessProbeIsSet() const;
    void UnsetReadinessProbe();
    void SetReadinessProbe(const std::shared_ptr<V1Probe> &value);

    std::shared_ptr<V1ResourceRequirements> GetResources() const;
    bool ResourcesIsSet() const;
    void UnsetResources();
    void SetResources(const std::shared_ptr<V1ResourceRequirements> &value);

    std::shared_ptr<V1SecurityContext> GetSecurityContext() const;
    bool SecurityContextIsSet() const;
    void UnSetSecurityContext();
    void SetSecurityContext(const std::shared_ptr<V1SecurityContext> &value);

    std::vector<std::shared_ptr<V1VolumeMount>> &GetVolumeMounts();
    bool VolumeMountsIsSet() const;
    void UnsetVolumeMounts();
    void SetVolumeMounts(const std::vector<std::shared_ptr<V1VolumeMount>> &value);

    std::string GetWorkingDir() const;
    bool WorkingDirIsSet() const;
    void UnsetWorkingDir();
    void SetWorkingDir(const std::string &value);

    std::string GetTerminationMessagePath() const;
    bool TerminationMessagePathIsSet() const;
    void UnsetTerminationMessagePath();
    void SetTerminationMessagePath(const std::string &value);

    std::string GetTerminationMessagePolicy() const;
    bool TerminationMessagePolicyIsSet() const;
    void UnsetTerminationMessagePolicy();
    void SetTerminationMessagePolicy(const std::string &value);

protected:
    std::vector<std::string> m_args;
    bool m_argsIsSet;
    std::vector<std::string> m_command;
    bool m_commandIsSet;
    std::vector<std::shared_ptr<V1EnvVar>> m_env;
    bool m_envIsSet;
    std::vector<std::shared_ptr<V1EnvFromSource>> m_envFrom;
    bool m_envFromIsSet;
    std::string m_image;
    bool m_imageIsSet;
    std::shared_ptr<V1Lifecycle> m_lifecycle;
    bool m_lifecycleIsSet;
    std::shared_ptr<V1Probe> m_livenessProbe;
    bool m_livenessProbeIsSet;
    std::string m_name;
    bool m_nameIsSet;
    std::vector<std::shared_ptr<V1ContainerPort>> m_ports;
    bool m_portsIsSet;
    std::shared_ptr<V1Probe> m_readinessProbe;
    bool m_readinessProbeIsSet;
    std::shared_ptr<V1ResourceRequirements> m_resources;
    bool m_resourcesIsSet;
    std::shared_ptr<V1SecurityContext> m_securityContext;
    bool m_securityContextIsSet;
    std::vector<std::shared_ptr<V1VolumeMount>> m_volumeMounts;
    bool m_volumeMountsIsSet;
    std::string m_workingDir;
    bool m_workingDirIsSet;
    std::string m_terminationMessagePath;
    bool m_terminationMessagePathIsSet;
    std::string m_terminationMessagePolicy;
    bool m_terminationMessagePolicyIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_H_ */
