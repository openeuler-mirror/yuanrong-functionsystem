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

#include "v1_container.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Container);

V1Container::V1Container()
{
    m_argsIsSet = false;
    m_commandIsSet = false;
    m_envIsSet = false;
    m_envFromIsSet = false;
    m_image = std::string("");
    m_imageIsSet = false;
    m_lifecycleIsSet = false;
    m_livenessProbeIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_portsIsSet = false;
    m_readinessProbeIsSet = false;
    m_resourcesIsSet = false;
    m_securityContextIsSet = false;
    m_volumeMountsIsSet = false;
    m_workingDir = std::string("");
    m_workingDirIsSet = false;
    m_terminationMessagePathIsSet = false;
    m_terminationMessagePolicyIsSet = false;
}

V1Container::~V1Container()
{
}

nlohmann::json V1Container::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_argsIsSet) {
        val["args"] = ModelUtils::ToJson(m_args);
    }
    if (m_commandIsSet) {
        val["command"] = ModelUtils::ToJson(m_command);
    }
    if (m_envIsSet) {
        val["env"] = ModelUtils::ToJson(m_env);
    }
    if (m_envFromIsSet) {
        val["envFrom"] = ModelUtils::ToJson(m_envFrom);
    }
    if (m_imageIsSet) {
        val["image"] = ModelUtils::ToJson(m_image);
    }
    if (m_lifecycleIsSet) {
        val["lifecycle"] = ModelUtils::ToJson(m_lifecycle);
    }
    if (m_livenessProbeIsSet) {
        val["livenessProbe"] = ModelUtils::ToJson(m_livenessProbe);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_portsIsSet) {
        val["ports"] = ModelUtils::ToJson(m_ports);
    }
    if (m_readinessProbeIsSet) {
        val["readinessProbe"] = ModelUtils::ToJson(m_readinessProbe);
    }
    if (m_resourcesIsSet) {
        val["resources"] = ModelUtils::ToJson(m_resources);
    }
    if (m_securityContextIsSet) {
        val["securityContext"] = ModelUtils::ToJson(m_securityContext);
    }
    if (m_volumeMountsIsSet) {
        val["volumeMounts"] = ModelUtils::ToJson(m_volumeMounts);
    }
    if (m_workingDirIsSet) {
        val["workingDir"] = ModelUtils::ToJson(m_workingDir);
    }
    if (m_terminationMessagePathIsSet) {
        val["terminationMessagePath"] = ModelUtils::ToJson(m_terminationMessagePath);
    }
    if (m_terminationMessagePolicyIsSet) {
        val["terminationMessagePolicy"] = ModelUtils::ToJson(m_terminationMessagePolicy);
    }
    return val;
}

bool V1Container::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val);
    ok &= ParseProbeInfoFromJson(val);
    if (val.contains("lifecycle")) {
        const nlohmann::json &fieldValue = val.at("lifecycle");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1Lifecycle> refValSetLifecycle;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLifecycle);
            SetLifecycle(refValSetLifecycle);
        }
    }
    if (val.contains("resources")) {
        const nlohmann::json &fieldValue = val.at("resources");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ResourceRequirements> refValSetResources;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResources);
            SetResources(refValSetResources);
        }
    }
    if (val.contains("securityContext")) {
        const nlohmann::json &fieldValue = val.at("securityContext");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1SecurityContext> refValSetSecurityContext;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSecurityContext);
            SetSecurityContext(refValSetSecurityContext);
        }
    }
    if (val.contains("volumeMounts")) {
        const nlohmann::json &fieldValue = val.at("volumeMounts");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1VolumeMount>> refValSetVolumeMounts;
            ok &= ModelUtils::FromJson(fieldValue, refValSetVolumeMounts);
            SetVolumeMounts(refValSetVolumeMounts);
        }
    }
    if (val.contains("workingDir")) {
        const nlohmann::json &fieldValue = val.at("workingDir");
        if (!fieldValue.is_null()) {
            std::string refValSetWorkingDir;
            ok &= ModelUtils::FromJson(fieldValue, refValSetWorkingDir);
            SetWorkingDir(refValSetWorkingDir);
        }
    }
    return ok;
}

bool V1Container::ParseBaseFromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("args")) {
        const nlohmann::json &fieldValue = val.at("args");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetArgs;
            ok &= ModelUtils::FromJson(fieldValue, refValSetArgs);
            SetArgs(refValSetArgs);
        }
    }
    if (val.contains("command")) {
        const nlohmann::json &fieldValue = val.at("command");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetCommand;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCommand);
            SetCommand(refValSetCommand);
        }
    }
    if (val.contains("env")) {
        const nlohmann::json &fieldValue = val.at("env");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1EnvVar>> refValSetEnv;
            ok &= ModelUtils::FromJson(fieldValue, refValSetEnv);
            SetEnv(refValSetEnv);
        }
    }
    if (val.contains("envFrom")) {
        const nlohmann::json &fieldValue = val.at("envFrom");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1EnvFromSource>> refValSetEnvFrom;
            ok &= ModelUtils::FromJson(fieldValue, refValSetEnvFrom);
            SetEnvFrom(refValSetEnvFrom);
        }
    }
    if (val.contains("image")) {
        const nlohmann::json &fieldValue = val.at("image");
        if (!fieldValue.is_null()) {
            std::string refValSetImage;
            ok &= ModelUtils::FromJson(fieldValue, refValSetImage);
            SetImage(refValSetImage);
        }
    }
    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains("ports")) {
        const nlohmann::json &fieldValue = val.at("ports");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1ContainerPort>> refValSetPorts;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPorts);
            SetPorts(refValSetPorts);
        }
    }
    return ok;
}

bool V1Container::ParseProbeInfoFromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("livenessProbe")) {
        const nlohmann::json &fieldValue = val.at("livenessProbe");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1Probe> refValSetLivenessProbe;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLivenessProbe);
            SetLivenessProbe(refValSetLivenessProbe);
        }
    }
    if (val.contains("readinessProbe")) {
        const nlohmann::json &fieldValue = val.at("readinessProbe");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1Probe> refValSetReadinessProbe;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReadinessProbe);
            SetReadinessProbe(refValSetReadinessProbe);
        }
    }
    if (val.contains("terminationMessagePath")) {
        const nlohmann::json &fieldValue = val.at("terminationMessagePath");
        if (!fieldValue.is_null()) {
            std::string refValSetTerminationMessagePath;
            ok = ok && ModelUtils::FromJson(fieldValue, refValSetTerminationMessagePath);
            SetTerminationMessagePath(refValSetTerminationMessagePath);
        }
    }
    if (val.contains("terminationMessagePolicy")) {
        const nlohmann::json &fieldValue = val.at("terminationMessagePolicy");
        if (!fieldValue.is_null()) {
            std::string refValSetTerminationMessagePolicy;
            ok = ok && ModelUtils::FromJson(fieldValue, refValSetTerminationMessagePolicy);
            SetTerminationMessagePolicy(refValSetTerminationMessagePolicy);
        }
    }
    return ok;
}

std::vector<std::string> &V1Container::GetArgs()
{
    return m_args;
}

void V1Container::SetArgs(const std::vector<std::string> &value)
{
    m_args = value;
    m_argsIsSet = true;
}

bool V1Container::ArgsIsSet() const
{
    return m_argsIsSet;
}

void V1Container::UnsetArgs()
{
    m_argsIsSet = false;
}

std::vector<std::string> &V1Container::GetCommand()
{
    return m_command;
}

void V1Container::SetCommand(const std::vector<std::string> &value)
{
    m_command = value;
    m_commandIsSet = true;
}

bool V1Container::CommandIsSet() const
{
    return m_commandIsSet;
}

void V1Container::UnsetCommand()
{
    m_commandIsSet = false;
}

std::vector<std::shared_ptr<V1EnvVar>> &V1Container::GetEnv()
{
    return m_env;
}

void V1Container::SetEnv(const std::vector<std::shared_ptr<V1EnvVar>> &value)
{
    m_env = value;
    m_envIsSet = true;
}

bool V1Container::EnvIsSet() const
{
    return m_envIsSet;
}

void V1Container::UnsetEnv()
{
    m_envIsSet = false;
}

std::vector<std::shared_ptr<V1EnvFromSource>>& V1Container::GetEnvFrom()
{
    return m_envFrom;
}

void V1Container::SetEnvFrom(const std::vector<std::shared_ptr<V1EnvFromSource>>& value)
{
    m_envFrom = value;
    m_envFromIsSet = true;
}

bool V1Container::EnvFromIsSet() const
{
    return m_envFromIsSet;
}

void V1Container::UnsetEnvFrom()
{
    m_envFromIsSet = false;
}

std::string V1Container::GetImage() const
{
    return m_image;
}

void V1Container::SetImage(const std::string &value)
{
    m_image = value;
    m_imageIsSet = true;
}

bool V1Container::ImageIsSet() const
{
    return m_imageIsSet;
}

void V1Container::UnsetImage()
{
    m_imageIsSet = false;
}

std::shared_ptr<V1Lifecycle> V1Container::GetLifecycle() const
{
    return m_lifecycle;
}

void V1Container::SetLifecycle(const std::shared_ptr<V1Lifecycle> &value)
{
    m_lifecycle = value;
    m_lifecycleIsSet = true;
}

bool V1Container::LifecycleIsSet() const
{
    return m_lifecycleIsSet;
}

void V1Container::UnsetLifecycle()
{
    m_lifecycleIsSet = false;
}

std::shared_ptr<V1Probe> V1Container::GetLivenessProbe() const
{
    return m_livenessProbe;
}

void V1Container::SetLivenessProbe(const std::shared_ptr<V1Probe> &value)
{
    m_livenessProbe = value;
    m_livenessProbeIsSet = true;
}

bool V1Container::LivenessProbeIsSet() const
{
    return m_livenessProbeIsSet;
}

void V1Container::UnsetLivenessProbe()
{
    m_livenessProbeIsSet = false;
}

std::string V1Container::GetName() const
{
    return m_name;
}

void V1Container::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1Container::NameIsSet() const
{
    return m_nameIsSet;
}

void V1Container::UnsetName()
{
    m_nameIsSet = false;
}

std::vector<std::shared_ptr<V1ContainerPort>> &V1Container::GetPorts()
{
    return m_ports;
}

void V1Container::SetPorts(const std::vector<std::shared_ptr<V1ContainerPort>> &value)
{
    m_ports = value;
    m_portsIsSet = true;
}

bool V1Container::PortsIsSet() const
{
    return m_portsIsSet;
}

void V1Container::UnsetPorts()
{
    m_portsIsSet = false;
}
std::shared_ptr<V1Probe> V1Container::GetReadinessProbe() const
{
    return m_readinessProbe;
}

void V1Container::SetReadinessProbe(const std::shared_ptr<V1Probe> &value)
{
    m_readinessProbe = value;
    m_readinessProbeIsSet = true;
}

bool V1Container::ReadinessProbeIsSet() const
{
    return m_readinessProbeIsSet;
}

void V1Container::UnsetReadinessProbe()
{
    m_readinessProbeIsSet = false;
}

std::shared_ptr<V1ResourceRequirements> V1Container::GetResources() const
{
    return m_resources;
}

void V1Container::SetResources(const std::shared_ptr<V1ResourceRequirements> &value)
{
    m_resources = value;
    m_resourcesIsSet = true;
}

bool V1Container::ResourcesIsSet() const
{
    return m_resourcesIsSet;
}

void V1Container::UnsetResources()
{
    m_resourcesIsSet = false;
}
std::shared_ptr<V1SecurityContext> V1Container::GetSecurityContext() const
{
    return m_securityContext;
}

void V1Container::SetSecurityContext(const std::shared_ptr<V1SecurityContext> &value)
{
    m_securityContext = value;
    m_securityContextIsSet = true;
}

bool V1Container::SecurityContextIsSet() const
{
    return m_securityContextIsSet;
}

void V1Container::UnSetSecurityContext()
{
    m_securityContextIsSet = false;
}

std::vector<std::shared_ptr<V1VolumeMount>> &V1Container::GetVolumeMounts()
{
    return m_volumeMounts;
}

void V1Container::SetVolumeMounts(const std::vector<std::shared_ptr<V1VolumeMount>> &value)
{
    m_volumeMounts = value;
    m_volumeMountsIsSet = true;
}

bool V1Container::VolumeMountsIsSet() const
{
    return m_volumeMountsIsSet;
}

void V1Container::UnsetVolumeMounts()
{
    m_volumeMountsIsSet = false;
}
std::string V1Container::GetWorkingDir() const
{
    return m_workingDir;
}

void V1Container::SetWorkingDir(const std::string &value)
{
    m_workingDir = value;
    m_workingDirIsSet = true;
}

bool V1Container::WorkingDirIsSet() const
{
    return m_workingDirIsSet;
}

void V1Container::UnsetWorkingDir()
{
    m_workingDirIsSet = false;
}

std::string V1Container::GetTerminationMessagePath() const
{
    return m_terminationMessagePath;
}

void V1Container::SetTerminationMessagePath(const std::string& value)
{
    m_terminationMessagePath = value;
    m_terminationMessagePathIsSet = true;
}

bool V1Container::TerminationMessagePathIsSet() const
{
    return m_terminationMessagePathIsSet;
}

void V1Container::UnsetTerminationMessagePath()
{
    m_terminationMessagePathIsSet = false;
}

std::string V1Container::GetTerminationMessagePolicy() const
{
    return m_terminationMessagePolicy;
}

void V1Container::SetTerminationMessagePolicy(const std::string& value)
{
    m_terminationMessagePolicy = value;
    m_terminationMessagePolicyIsSet = true;
}

bool V1Container::TerminationMessagePolicyIsSet() const
{
    return m_terminationMessagePolicyIsSet;
}

void V1Container::UnsetTerminationMessagePolicy()
{
    m_terminationMessagePolicyIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
