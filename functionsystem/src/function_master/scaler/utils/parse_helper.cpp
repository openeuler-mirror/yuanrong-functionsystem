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

#include "parse_helper.h"

#include "common/constants/constants.h"
#include "common/status/status.h"
#include "common/types/instance_state.h"
#include "utils/string_utils.hpp"

namespace functionsystem {
const std::set<std::string> SENSITIVE_VOLUME_NAME = { "sts-config" };
const std::string CPU_RESOURCE = "cpu";
const std::set<std::string> BIND_CPU_VALUE_SET = { "24000m", "24", "48000m", "48", "72000m", "72", "96000m", "96" };
const std::string INTERNAL_IP_TYPE = "InternalIP";
const std::string DELEGATE_POD_LABELS = "DELEGATE_POD_LABELS";
const std::string DELEGATE_POD_INIT_LABELS = "DELEGATE_POD_INIT_LABELS";
const std::string DELEGATE_NODE_AFFINITY_POLICY = "DELEGATE_NODE_AFFINITY_POLICY";
const std::string NODE_AFFINITY_POLICY_AGGREGATION = "aggregation";
const std::string GRACEFUL_SHUTDOWN_LABEL_KEY = "gracefulTerminating";
const std::string INSTANCE_STATUS_LABEL_KEY = "instanceStatus";
const std::string REUSE_LABEL_KEY = "yr-reuse";
const std::string APP_LABEL_KEY = "app";
const std::string LABEL_SCHEDULE_POLICY = "schedule-policy";
const std::string ANNOTATION_KEY_PREFIX = "yr-labels-";
const std::string POD_POOL_ID = "yr-pod-pool";
const std::string CPU_UNIT = "m";
const std::string MEM_UNIT_MB = "Mi";
const std::string MEM_UNIT_GB = "Gi";
const int CPU_SIZE_MILL = 1000;
const int MEM_SIZE_MB = 1024;
const int LABEL_SPLIT_ITEM_NUM = 2;
const int NODE_AFFINITY_MAX_LEN = 10;
const int MAX_LABEL_LEN = 63;

static std::unordered_map<resource_view::AffinityType, bool> IsPreferred = {
    { resource_view::AffinityType::PreferredAffinity, true },
    { resource_view::AffinityType::PreferredAntiAffinity, true },
    { resource_view::AffinityType::RequiredAffinity, false },
    { resource_view::AffinityType::RequiredAntiAffinity, false }
};

static std::unordered_map<resource_view::AffinityType, bool> IsAnti = {
    { resource_view::AffinityType::PreferredAffinity, false },
    { resource_view::AffinityType::PreferredAntiAffinity, true },
    { resource_view::AffinityType::RequiredAffinity, false },
    { resource_view::AffinityType::RequiredAntiAffinity, true }
};

static std::unordered_map<PodPhase, std::string> PodPhase2StrMap = {
    {PodPhase::PENDING, "Pending"},
    {PodPhase::RUNNING, "Running"},
    {PodPhase::SUCCEEDED, "Succeeded"},
    {PodPhase::FAILED, "Failed"},
    {PodPhase::UNKNOWN, "Unknown"}
};

void ParseDelegateContainer(const ::resources::InstanceInfo &instanceInfo,
                            litebus::Option<std::shared_ptr<V1Container>> &delegateContainer)
{
    auto iter = instanceInfo.createoptions().find(DELEGATE_CONTAINER);
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_CONTAINER");
        return;
    }
    delegateContainer = ParseContainer(parser);
}

void ParseDelegateRuntimeManager(const ::resources::InstanceInfo &instanceInfo,
                                 litebus::Option<std::shared_ptr<V1Container>> &delegateRuntimeManager)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_RUNTIME_MANAGER");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_RUNTIME_MANAGER");
        return;
    }

    auto container = std::make_shared<V1Container>();
    if (auto iter = parser.find("image"); iter != parser.end()) {
        container->SetImage(iter.value());
    }

    if (auto iter = parser.find("env"); iter != parser.end()) {
        container->SetEnv(ParseContainerEnv(iter.value()));
    }

    delegateRuntimeManager = container;
}

void ParseDelegateSidecars(const ::resources::InstanceInfo &instanceInfo,
                           std::vector<std::shared_ptr<V1Container>> &delegateSidecars)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_SIDECARS");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_SIDECARS");
        return;
    }

    if (!json.is_array()) {
        YRLOG_ERROR("illegal DELEGATE_SIDECARS");
        return;
    }

    for (const auto &sidecar : json) {
        auto container = ParseContainer(sidecar);
        if (container.IsNone()) {
            YRLOG_ERROR("failed to parse sidecar, invalid container");
            continue;
        }
        delegateSidecars.push_back(std::move(container.Get()));
    }
}

void ParseDelegateInitContainers(const ::resources::InstanceInfo &instanceInfo,
                                 std::vector<std::shared_ptr<V1Container>> &delegateInitContainers)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_INIT_CONTAINERS");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_INIT_CONTAINERS");
        return;
    }

    if (!json.is_array()) {
        YRLOG_ERROR("illegal DELEGATE_INIT_CONTAINERS");
        return;
    }

    for (const auto &initContainer : json) {
        auto container = ParseContainer(initContainer);
        if (container.IsNone()) {
            YRLOG_ERROR("failed to parse init container, invalid container");
            continue;
        }
        delegateInitContainers.push_back(std::move(container.Get()));
    }
}

void ParseEnvValueFrom(const nlohmann::json &parser, const std::shared_ptr<V1EnvVar> &envVar)
{
    auto valueFrom = parser.find("valueFrom");
    if (valueFrom == parser.end()) {
        return;
    }

    auto envVarSource = std::make_shared<V1EnvVarSource>();

    if (auto fieldRef = valueFrom->find("fieldRef"); fieldRef != valueFrom->end()) {
        auto objectFieldSelector = std::make_shared<V1ObjectFieldSelector>();
        if (auto fieldPath = fieldRef->find("fieldPath"); fieldPath != fieldRef->end()) {
            objectFieldSelector->SetFieldPath(fieldPath.value());
        }
        if (auto apiVersion = fieldRef->find("apiVersion"); apiVersion != fieldRef->end()) {
            objectFieldSelector->SetApiVersion(apiVersion.value());
        }
        envVarSource->SetFieldRef(objectFieldSelector);
    } else if (auto resourceFieldRef = valueFrom->find("resourceFieldRef"); resourceFieldRef != valueFrom->end()) {
        auto resourceFieldSelector = std::make_shared<V1ResourceFieldSelector>();
        if (auto resource = resourceFieldRef->find("resource"); resource != resourceFieldRef->end()) {
            resourceFieldSelector->SetResource(resource.value());
        }
        if (auto containerName = resourceFieldRef->find("containerName"); containerName != resourceFieldRef->end()) {
            resourceFieldSelector->SetContainerName(containerName.value());
        }
        if (auto divisor = resourceFieldRef->find("divisor"); divisor != resourceFieldRef->end()) {
            resourceFieldSelector->SetDivisor(divisor.value());
        }
        envVarSource->SetResourceFieldRef(resourceFieldSelector);
    } else if (auto secretKeyRef = valueFrom->find("secretKeyRef"); secretKeyRef != valueFrom->end()) {
        auto secretKeyRefSelector = std::make_shared<V1SecretKeySelector>();
        if (auto name = secretKeyRef->find("name"); name != secretKeyRef->end()) {
            secretKeyRefSelector->SetName(name.value());
        }
        if (auto key = secretKeyRef->find("key"); key != secretKeyRef->end()) {
            secretKeyRefSelector->SetKey(key.value());
        }
        envVarSource->SetSecretKeyRef(secretKeyRefSelector);
    } else {
        YRLOG_ERROR("not support valueFrom type.");
    }

    envVar->SetValueFrom(envVarSource);
}

std::vector<std::shared_ptr<V1EnvVar>> ParseContainerEnv(const nlohmann::json &parser)
{
    std::vector<std::shared_ptr<V1EnvVar>> envs;
    if (!parser.is_array()) {
        YRLOG_ERROR("illegal env");
        return envs;
    }

    for (const auto &iterator : parser) {
        auto envVar = std::make_shared<V1EnvVar>();
        if (auto name = iterator.find("name"); name != iterator.end()) {
            envVar->SetName(name.value());
        }
        if (auto value = iterator.find("value"); value != iterator.end()) {
            envVar->SetValue(value.value());
        } else {
            ParseEnvValueFrom(iterator, envVar);
        }
        (void)envs.emplace_back(envVar);
    }
    return envs;
}

std::vector<std::shared_ptr<V1EnvVar>> ParseContainerEnvFromMap(const std::map<std::string, std::string> &environments)
{
    std::vector<std::shared_ptr<V1EnvVar>> envs;
    for (const auto &item : environments) {
        if (item.first.empty()) {
            continue;
        }
        auto envVar = std::make_shared<V1EnvVar>();
        envVar->SetName(item.first);
        envVar->SetValue(item.second);
        envs.push_back(envVar);
    }
    return envs;
}

std::shared_ptr<V1ResourceRequirements> ParseResourceRequirements(const nlohmann::json &parser)
{
    auto resourceRequirements = std::make_shared<V1ResourceRequirements>();
    if (auto iter = parser.find("limits"); iter != parser.end() && iter->is_object()) {
        std::map<std::string, std::string> limitMap;
        for (const auto &limit : iter->items()) {
            limitMap.emplace(limit.key(), limit.value());
        }
        resourceRequirements->SetLimits(limitMap);
    }

    if (auto iter = parser.find("requests"); iter != parser.end() && iter->is_object()) {
        std::map<std::string, std::string> requestMap;
        for (const auto &request : iter->items()) {
            (void)requestMap.emplace(request.key(), request.value());
        }
        resourceRequirements->SetRequests(requestMap);
    }
    return resourceRequirements;
}

void ParseVolumeMounts(const nlohmann::json &parser, std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts,
                       bool needFilterSensitiveVolume)
{
    if (!parser.is_array()) {
        YRLOG_ERROR("illegal volume mount");
        return;
    }

    for (const auto &volumeMount : parser) {
        auto name = volumeMount.find("name");
        if (name == volumeMount.end()) {
            YRLOG_ERROR("illegal volumeMount.name");
            continue;
        }

        if (needFilterSensitiveVolume && SENSITIVE_VOLUME_NAME.find(name.value()) != SENSITIVE_VOLUME_NAME.end()) {
            YRLOG_ERROR("failed to mount volume({}), sensitive volume", name.value().dump());
            continue;
        }

        auto mountPath = volumeMount.find("mountPath");
        if (mountPath == volumeMount.end()) {
            YRLOG_ERROR("illegal volumeMount.mountPath");
            continue;
        }

        auto volumeMountPtr = std::make_shared<V1VolumeMount>();
        volumeMountPtr->SetName(name.value());
        volumeMountPtr->SetMountPath(mountPath.value());

        if (auto readOnly = volumeMount.find("readOnly"); readOnly != volumeMount.end()) {
            volumeMountPtr->SetReadOnly(readOnly.value());
        }
        if (auto subPath = volumeMount.find("subPath"); subPath != volumeMount.end()) {
            volumeMountPtr->SetSubPath(subPath.value());
        }
        if (auto subPathExpr = volumeMount.find("subPathExpr"); subPathExpr != volumeMount.end()) {
            volumeMountPtr->SetSubPathExpr(subPathExpr.value());
        }
        if (auto mountPropagation = volumeMount.find("mountPropagation"); mountPropagation != volumeMount.end()) {
            volumeMountPtr->SetMountPropagation(mountPropagation.value());
        }

        (void)volumeMounts.emplace_back(volumeMountPtr);
    }
}

std::shared_ptr<V1ExecAction> ParseExecAction(const nlohmann::json &parser)
{
    auto execAction = std::make_shared<V1ExecAction>();
    if (auto iter = parser.find("command"); iter != parser.end() && iter.value().is_array()) {
        std::vector<std::string> command;
        for (const auto &item : iter.value()) {
            command.push_back(item);
        }
        execAction->SetCommand(command);
    }
    return execAction;
}

std::shared_ptr<V1LifecycleHandler> ParseLifecycleHandler(const nlohmann::json &parser)
{
    auto lifecycleHandler = std::make_shared<V1LifecycleHandler>();
    if (auto iter = parser.find("exec"); iter != parser.end()) {
        lifecycleHandler->SetExec(ParseExecAction(iter.value()));
    }
    return lifecycleHandler;
}

std::shared_ptr<V1Lifecycle> ParseLifecycle(const nlohmann::json &parser)
{
    auto lifecycle = std::make_shared<V1Lifecycle>();
    if (auto iter = parser.find("preStop"); iter != parser.end()) {
        lifecycle->SetPreStop(ParseLifecycleHandler(iter.value()));
    }

    if (auto iter = parser.find("postStart"); iter != parser.end()) {
        lifecycle->SetPostStart(ParseLifecycleHandler(iter.value()));
    }
    return lifecycle;
}

std::shared_ptr<V1Probe> ParseV1Probe(const nlohmann::json &parser)
{
    auto probe = std::make_shared<V1Probe>();
    if (auto iter = parser.find("exec"); iter != parser.end()) {
        probe->SetExec(ParseExecAction(iter.value()));
    }

    if (auto iter = parser.find("initialDelaySeconds"); iter != parser.end() && iter.value().is_number()) {
        probe->SetInitialDelaySeconds(iter.value());
    }

    if (auto iter = parser.find("timeoutSeconds"); iter != parser.end() && iter.value().is_number()) {
        probe->SetTimeoutSeconds(iter.value());
    }

    if (auto iter = parser.find("periodSeconds"); iter != parser.end() && iter.value().is_number()) {
        probe->SetPeriodSeconds(iter.value());
    }

    if (auto iter = parser.find("successThreshold"); iter != parser.end() && iter.value().is_number()) {
        probe->SetSuccessThreshold(iter.value());
    }

    if (auto iter = parser.find("failureThreshold"); iter != parser.end() && iter.value().is_number()) {
        probe->SetFailureThreshold(iter.value());
    }
    return probe;
}

void ParseContainerInfo(const nlohmann::json &parser, const std::shared_ptr<V1Container> &container)
{
    if (auto iter = parser.find("name"); iter != parser.end()) {
        container->SetName(iter.value());
    }

    if (auto iter = parser.find("image"); iter != parser.end()) {
        container->SetImage(iter.value());
    }

    if (parser.find("command") != parser.end()) {
        std::vector<std::string> command;
        for (auto &item : parser.at("command")) {
            command.push_back(item);
        }
        container->SetCommand(command);
    }

    if (parser.find("workingDir") != parser.end()) {
        container->SetWorkingDir(parser.at("workingDir"));
    }

    if (parser.find("args") != parser.end()) {
        std::vector<std::string> args;
        for (auto &item : parser.at("args")) {
            args.push_back(item);
        }
        container->SetArgs(args);
    }

    int32_t uid = INVALID_ID;
    int32_t gid = INVALID_ID;
    if (auto iter = parser.find("uid"); iter != parser.end() && iter.value().is_number()) {
        uid = static_cast<int32_t>(iter.value()) > 0 ? static_cast<int32_t>(iter.value()) : INVALID_ID;
    }

    if (auto iter = parser.find("gid"); iter != parser.end() && iter.value().is_number()) {
        gid = static_cast<int32_t>(iter.value()) > 0 ? static_cast<int32_t>(iter.value()) : INVALID_ID;
    }

    auto context = std::make_shared<V1SecurityContext>();
    if (uid == INVALID_ID || gid == INVALID_ID) {
        // if uid or gid isn't set, set runAsNonRoot false
        context->SetRunAsNonRoot(false);
    } else {
        context->SetRunAsGroup(gid);
        context->SetRunAsUser(uid);
    }
    container->SetSecurityContext(context);
}

litebus::Option<std::shared_ptr<V1Container>> ParseContainer(const nlohmann::json &parser)
{
    auto container = std::make_shared<V1Container>();
    ParseContainerInfo(parser, container);

    if (const auto &iter = parser.find("volumeMounts"); iter != parser.end()) {
        std::vector<std::shared_ptr<V1VolumeMount>> volumeMounts;
        // need to filter sensitive volumes, when mount onto user's container
        ParseVolumeMounts(iter.value(), volumeMounts, true);
        container->SetVolumeMounts(volumeMounts);
    }

    if (auto iter = parser.find("env"); iter != parser.end()) {
        auto envs = ParseContainerEnv(iter.value());
        if (!envs.empty()) {
            container->SetEnv(envs);
        }
    }

    if (auto iter = parser.find("resourceRequirements"); iter != parser.end()) {
        container->SetResources(ParseResourceRequirements(iter.value()));
    }

    if (auto iter = parser.find("lifecycle"); iter != parser.end()) {
        container->SetLifecycle(ParseLifecycle(iter.value()));
    }

    if (auto iter = parser.find("livenessProbe"); iter != parser.end() && !iter.value().empty()) {
        container->SetLivenessProbe(ParseV1Probe(iter.value()));
    }

    if (auto iter = parser.find("readinessProbe"); iter != parser.end() && !iter.value().empty()) {
        container->SetReadinessProbe(ParseV1Probe(iter.value()));
    }
    return container;
}

void ParseDelegateHostAliases(const ::resources::InstanceInfo &instanceInfo,
                              std::vector<std::shared_ptr<V1HostAlias>> &delegateHostAliases)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_HOST_ALIASES");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json hostAliases;
    try {
        hostAliases = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &error) {
        YRLOG_ERROR("failed to get DELEGATE_HOST_ALIASES, parse invalid error: ", error.what());
        return;
    }

    if (!hostAliases.is_object()) {
        YRLOG_ERROR("illegal hostAliases in DELEGATE_HOST_ALIASES.");
        return;
    }

    for (const auto &item : hostAliases.items()) {
        if (!item.value().is_array() || item.value().empty()) {
            YRLOG_WARN("invalid host alias({})", item.key());
            continue;
        }

        auto hostAlias = std::make_shared<V1HostAlias>();
        hostAlias->SetIp(item.key());
        hostAlias->SetHostnames(item.value());
        (void)delegateHostAliases.emplace_back(hostAlias);
    }
}

void ParseDelegateDecryptEnv(const ::resources::InstanceInfo &instanceInfo,
                             std::vector<std::shared_ptr<V1EnvVar>> &delegateDecryptEnvs)
{
    auto iter = instanceInfo.createoptions().find(DELEGATE_DECRYPT);
    if (iter == instanceInfo.createoptions().end()) {
        // Compatible
        iter = instanceInfo.createoptions().find(DELEGATE_ENCRYPT);
    }

    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &error) {
        YRLOG_ERROR("failed to decrypt delegate env, parse invalid DELEGATE_DECRYPT json error: ", error.what());
        return;
    }

    for (const auto &item : parser.items()) {
        if (item.key() != "environment") {
            continue;
        }

        nlohmann::json envsParser;
        try {
            envsParser = nlohmann::json::parse(std::string(item.value()));  // litebus::option::Get()
        } catch (nlohmann::json::parse_error &error) {
            YRLOG_ERROR("failed to decrypt delegate env, parse invalid env json error: ", error.what());
            break;
        }

        for (const auto &env : envsParser.items()) {
            auto envVar = std::make_shared<V1EnvVar>();
            envVar->SetName(env.key());
            // unable to auto convert non-string value into string (throw exception), so dump into string
            if (env.value().is_string()) {
                envVar->SetValue(env.value());
            } else {
                envVar->SetValue(env.value().dump());
            }
            (void)delegateDecryptEnvs.emplace_back(envVar);
        }
        break;
    }
}

void SetHostAliases(const std::shared_ptr<V1PodSpec> &spec,
                    const std::vector<std::shared_ptr<V1HostAlias>> &delegateHostAliases)
{
    if (delegateHostAliases.empty()) {
        return;
    }

    if (!spec->HostAliasesIsSet()) {
        spec->SetHostAliases(delegateHostAliases);
        return;
    }

    for (const auto &hostAlias : delegateHostAliases) {
        spec->GetHostAliases().push_back(hostAlias);
    }
}

bool ParseVolumeHostPath(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume)
{
    auto hostPath = json.find("hostPath");
    if (hostPath == json.end()) {
        return false;
    }

    auto hostPathVolumeSource = std::make_shared<V1HostPathVolumeSource>();
    if (auto path = hostPath->find("path"); path != hostPath->end()) {
        hostPathVolumeSource->SetPath(path.value());
    }
    if (auto type = hostPath->find("type"); type != hostPath->end()) {
        hostPathVolumeSource->SetType(type.value());
    }

    volume->SetHostPath(hostPathVolumeSource);
    return true;
}

bool ParseVolumeConfigMap(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume)
{
    auto configMap = json.find("configMap");
    if (configMap == json.end()) {
        return false;
    }

    auto configMapVolumeSource = std::make_shared<V1ConfigMapVolumeSource>();
    if (auto name = configMap->find("name"); name != configMap->end()) {
        configMapVolumeSource->SetName(name.value());
    }
    if (auto defaultMode = configMap->find("defaultMode"); defaultMode != configMap->end()) {
        configMapVolumeSource->SetDefaultMode(defaultMode.value());
    }
    if (auto iterator = configMap->find("items"); iterator != configMap->end() && iterator->is_array()) {
        for (const auto &value : iterator.value()) {
            auto keyToPath = std::make_shared<V1KeyToPath>();
            if (auto key = value.find("key"); key != value.end()) {
                keyToPath->SetKey(key.value());
            }
            if (auto mode = value.find("mode"); mode != value.end()) {
                keyToPath->SetMode(mode.value());
            }
            if (auto path = value.find("path"); path != value.end()) {
                keyToPath->SetPath(path.value());
            }
            (void)configMapVolumeSource->GetItems().emplace_back(keyToPath);
        }
    }

    volume->SetConfigMap(configMapVolumeSource);
    return true;
}

bool ParseVolumeEmptyDir(const nlohmann::json &json, std::shared_ptr<V1Volume> &volume)
{
    auto emptyDir = json.find("emptyDir");
    if (emptyDir == json.end()) {
        return false;
    }

    auto emptyDirVolumeSource = std::make_shared<V1EmptyDirVolumeSource>();
    if (auto medium = emptyDir->find("medium"); medium != emptyDir->end()) {
        emptyDirVolumeSource->SetMedium(medium.value());
    }
    if (auto sizeLimit = emptyDir->find("sizeLimit"); sizeLimit != emptyDir->end()) {
        emptyDirVolumeSource->SetSizeLimit(sizeLimit.value());
    }

    volume->SetEmptyDir(emptyDirVolumeSource);
    return true;
}

bool ParseVolumeSecret(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume)
{
    auto secret = json.find("secret");
    if (secret == json.end()) {
        return false;
    }

    auto secretVolumeSource = std::make_shared<V1SecretVolumeSource>();
    if (auto secretName = secret->find("secretName"); secretName != secret->end()) {
        secretVolumeSource->SetSecretName(secretName.value());
    }
    if (auto defaultMode = secret->find("defaultMode"); defaultMode != secret->end()) {
        secretVolumeSource->SetDefaultMode(defaultMode.value());
    }
    if (auto optional = secret->find("optional"); optional != secret->end()) {
        secretVolumeSource->SetOptional(optional.value());
    }
    if (auto iterator = secret->find("items"); iterator != secret->end() && iterator->is_array()) {
        std::vector<std::shared_ptr<V1KeyToPath>> items;
        for (const auto &value : iterator.value()) {
            auto secretKeyToPath = std::make_shared<V1KeyToPath>();
            if (auto key = value.find("key"); key != value.end()) {
                secretKeyToPath->SetKey(key.value());
            }
            if (auto mode = value.find("mode"); mode != value.end()) {
                secretKeyToPath->SetMode(mode.value());
            }
            if (auto path = value.find("path"); path != value.end()) {
                secretKeyToPath->SetPath(path.value());
            }
            (void)items.emplace_back(secretKeyToPath);
        }
        if (!items.empty()) {
            secretVolumeSource->SetItems(items);
        }
    }

    volume->SetSecret(secretVolumeSource);
    return true;
}

bool ParseVolumeProjected(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume)
{
    auto projected = json.find("projected");
    if (projected == json.end()) {
        return false;
    }
    std::shared_ptr<V1ProjectedVolumeSource> projectedVolumeSource;
    bool ok = functionsystem::kube_client::model::ModelUtils::FromJson(json.at("projected"), projectedVolumeSource);
    if (ok) {
        volume->SetProjected(projectedVolumeSource);
    }
    return ok;
}

bool ParseVolumePersistent(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume)
{
    auto persistent = json.find("persistentVolumeClaim");
    if (persistent == json.end()) {
        return false;
    }
    std::shared_ptr<V1PersistentVolumeClaimVolumeSource> persistentVolumeSource;
    bool ok = functionsystem::kube_client::model::ModelUtils::FromJson(json.at("persistentVolumeClaim"),
                                                                       persistentVolumeSource);
    if (ok) {
        volume->SetPersistentVolumeClaim(persistentVolumeSource);
    }
    return ok;
}

void ParseDelegateVolumes(const ::resources::InstanceInfo &instanceInfo,
                          std::vector<std::shared_ptr<V1Volume>> &volumes)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_VOLUMES");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    ParseVolumesFromJson(iter->second, volumes);
}

void ParseVolumesFromJson(const std::string jsonStr, std::vector<std::shared_ptr<V1Volume>> &volumes)
{
    if (jsonStr.empty()) {
        YRLOG_WARN("volume json str is empty.");
        return;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_VOLUMES.");
        return;
    }

    if (!json.is_array()) {
        YRLOG_ERROR("illegal DELEGATE_VOLUMES.");
        return;
    }

    for (const auto &item : json) {
        auto name = item.find("name");
        if (name == item.end()) {
            YRLOG_ERROR("illegal volume.name in DELEGATE_VOLUME.");
            continue;
        }

        auto volume = std::make_shared<V1Volume>();
        volume->SetName(name.value());

        // Chain of responsibility
        if (ParseVolumeHostPath(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }

        if (ParseVolumeConfigMap(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }

        if (ParseVolumeEmptyDir(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }

        if (ParseVolumeSecret(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }

        if (ParseVolumeProjected(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }
        if (ParseVolumePersistent(item, volume)) {
            (void)volumes.emplace_back(volume);
            continue;
        }
        YRLOG_WARN("{} volume's type is not support.", name.value().dump());
    }
}

void ParseDelegateTolerations(const ::resources::InstanceInfo &instanceInfo,
                              std::vector<std::shared_ptr<V1Toleration>> &delegateTolerations)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_TOLERATIONS");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    ParseDelegateTolerationsFromStr(iter->second, delegateTolerations);
}

void ParseDelegateTolerationsFromStr(const std::string &val,
                                     std::vector<std::shared_ptr<V1Toleration>> &delegateTolerations)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(val);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse tolerations.");
        return;
    }
    if (!json.is_array()) {
        YRLOG_ERROR("illegal tolerations.");
        return;
    }
    for (const auto &item : json) {
        auto effect = item.find("effect");
        if (effect == item.end()) {
            YRLOG_ERROR("illegal effect in DELEGATE_TOLERATIONS.");
            continue;
        }
        auto toleration = std::make_shared<V1Toleration>();
        toleration->FromJson(item);
        (void)delegateTolerations.emplace_back(toleration);
    }
}

void ParseDelegateTopologySpreadConstraintsFromStr(
    const std::string &val,
    std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &delegateTopologySpreadConstraints)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(val);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse topology spread constraints.");
        return;
    }
    if (!json.is_array()) {
        YRLOG_ERROR("illegal topology spread constraints.");
        return;
    }
    for (const auto &item : json) {
        auto effect = item.find("topologyKey");
        if (effect == item.end()) {
            YRLOG_WARN("illegal topologyKey in topology spread constraints, skip it.");
            continue;
        }
        auto topologySpreadConstraint = std::make_shared<V1TopologySpreadConstraint>();
        topologySpreadConstraint->FromJson(item);
        (void)&delegateTopologySpreadConstraints.emplace_back(topologySpreadConstraint);
    }
}

void ParseDelegateInitEnv(const ::resources::InstanceInfo &instanceInfo,
                          std::vector<std::shared_ptr<V1EnvVar>> &delegateInitEnvs)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_INIT_ENV");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }

    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &error) {
        YRLOG_ERROR("failed to decrypt delegate env, parse invalid DELEGATE_INIT_ENV json error: ", error.what());
        return;
    }
    delegateInitEnvs = ParseContainerEnv(parser);
}

void ParseAffinityFromStr(const std::string &val, const std::shared_ptr<V1Affinity> &affinity)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(val);
        affinity->FromJson(j);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse affinity info, error: {}", error.what());
    }
}

void ParseAffinityFromCreateOpts(const ::resources::InstanceInfo &instanceInfo,
                                 const std::shared_ptr<V1Affinity> &affinity)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_AFFINITY");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(iter->second);
        affinity->FromJson(json);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_AFFINITY.");
        return;
    }
}

void ParseNodeAffinity(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Affinity> &affinity)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_NODE_AFFINITY");
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_NODE_AFFINITY.");
        return;
    }
    auto nodeAffinity = std::make_shared<V1NodeAffinity>();
    nodeAffinity->FromJson(json);
    affinity->SetNodeAffinity(nodeAffinity);
}

std::shared_ptr<V1Affinity> ParseAffinity(
    const std::unordered_map<resource_view::AffinityType, std::vector<std::string>> &affinityTypeLabels)
{
    auto podAffinity = std::make_shared<V1PodAffinity>();
    std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> preferredWeightedPodAffinityTerm;
    std::vector<std::shared_ptr<V1PodAffinityTerm>> requiredPodAffinityTerm;

    auto podAntiAffinity = std::make_shared<V1PodAntiAffinity>();
    std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> preferredWeightedPodAntiAffinityTerm;
    std::vector<std::shared_ptr<V1PodAffinityTerm>> requiredPodAntiAffinityTerm;

    for (const auto &labels : affinityTypeLabels) {
        for (const auto &label : labels.second) {
            if (IsPreferred[labels.first]) {
                if (IsAnti[labels.first]) {
                    (void)preferredWeightedPodAntiAffinityTerm.emplace_back(CreateWeightedPodAffinityTerm(label));
                } else {
                    (void)preferredWeightedPodAffinityTerm.emplace_back(CreateWeightedPodAffinityTerm(label));
                }
            } else {
                if (IsAnti[labels.first]) {
                    (void)requiredPodAntiAffinityTerm.emplace_back(CreatePodAffinityTerm(label));
                } else {
                    (void)requiredPodAffinityTerm.emplace_back(CreatePodAffinityTerm(label));
                }
            }
        }
    }

    podAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution(preferredWeightedPodAffinityTerm);
    podAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(requiredPodAffinityTerm);
    podAntiAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution(preferredWeightedPodAntiAffinityTerm);
    podAntiAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(requiredPodAntiAffinityTerm);

    auto affinity = std::make_shared<V1Affinity>();
    affinity->SetPodAffinity(podAffinity);
    affinity->SetPodAntiAffinity(podAntiAffinity);
    return affinity;
}

std::shared_ptr<V1PodAffinityTerm> CreatePodAffinityTerm(const std::string &label)
{
    auto podAffinityTerm = std::make_shared<V1PodAffinityTerm>();
    podAffinityTerm->SetLabelSelector(CreateLabelSelector(label));
    podAffinityTerm->SetTopologyKey("kubernetes.io/hostname");
    return podAffinityTerm;
}

std::shared_ptr<V1LabelSelector> CreateLabelSelector(const std::string &label)
{
    auto labelSelector = std::make_shared<V1LabelSelector>();
    std::map<std::string, std::string> labels;
    auto items = litebus::strings::Split(label, ":");
    (void)labels.emplace(items[0], items.size() > 1 ? items[1] : "");
    labelSelector->SetMatchLabels(labels);
    return labelSelector;
}

std::shared_ptr<V1WeightedPodAffinityTerm> CreateWeightedPodAffinityTerm(const std::string &label)
{
    auto weightedPodAffinityTerm = std::make_shared<V1WeightedPodAffinityTerm>();
    weightedPodAffinityTerm->SetPodAffinityTerm(CreatePodAffinityTerm(label));
    weightedPodAffinityTerm->SetWeight(100);  // 100: default weight
    return weightedPodAffinityTerm;
}

std::unordered_map<resource_view::AffinityType, std::vector<std::string>> GetAffinityInfo(
    const ::resources::InstanceInfo &instanceInfo)
{
    auto affinity = instanceInfo.scheduleoption().affinity();
    auto labels = affinity.instanceaffinity().affinity();

    std::unordered_map<resource_view::AffinityType, std::vector<std::string>> affinityTypeLabels;
    for (const auto &label : labels) {
        (void)affinityTypeLabels[label.second].emplace_back(label.first);
    }

    return affinityTypeLabels;
}

std::map<std::string, std::string> GetPodLabelsForCreate(const ::resources::InstanceInfo &instanceInfo)
{
    std::map<std::string, std::string> allLabels;
    auto initLabelsStr = instanceInfo.createoptions().find(DELEGATE_POD_INIT_LABELS);
    if (initLabelsStr == instanceInfo.createoptions().end()) {
        YRLOG_DEBUG("{}|no init labels need to be added for instance({})", instanceInfo.requestid(),
                    instanceInfo.instanceid());
    } else {
        auto initLabels = ParseMapFromStr(initLabelsStr->second);
        for (const auto &label : initLabels) {
            (void)allLabels.emplace(label.first, label.second);
        }
    }
    // add labels for affinity
    for (const auto &label : instanceInfo.labels()) {
        auto items = litebus::strings::Split(label, ":");
        if (items.size() > LABEL_SPLIT_ITEM_NUM) {
            YRLOG_WARN("label key or value contains [:], label is {}", label);
        }
        (void)allLabels.emplace(items[0], items.size() > 1 ? items[1] : "");
    }
    return allLabels;
}

std::string GetPodIPFromFunctionAgentID(const std::string &functionAgentID)
{
    // function agent id format: function_agent_${function-agent-ip}-${function-agent-port}
    std::string agentAddress = functionAgentID;
    (void)litebus::strings::Trim(agentAddress, litebus::strings::Mode::PREFIX, FUNCTION_AGENT_ID_PREFIX);

    const uint32_t addressLen = 2;
    const uint32_t ipPos = 0;
    const std::string splitPattern = "-";

    std::vector<std::string> strs = litebus::strings::Split(agentAddress, splitPattern);
    if (strs.size() < addressLen) {
        YRLOG_ERROR("invalid agent address {}", agentAddress);
        return "";
    }
    return strs[ipPos];
}

std::map<std::string, std::string> GetLabelsFromCreateOpt(const ::resources::InstanceInfo &instanceInfo)
{
    auto labelsStr = instanceInfo.createoptions().find(DELEGATE_POD_LABELS);
    if (labelsStr == instanceInfo.createoptions().end()) {
        YRLOG_DEBUG("{}|no labels need to be added for instance({})", instanceInfo.requestid(),
                    instanceInfo.instanceid());
        return {};
    }
    return ParseMapFromStr(labelsStr->second);
}

std::shared_ptr<V1SeccompProfile> ParseSeccompProfile(const ::resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_POD_SECCOMP_PROFILE");
    if (iter == instanceInfo.createoptions().end()) {
        return nullptr;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(iter->second);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse DELEGATE_POD_SECCOMP_PROFILE.");
        return nullptr;
    }
    auto secCompProfile = std::make_shared<V1SeccompProfile>();
    secCompProfile->FromJson(json);
    return secCompProfile;
}

std::map<std::string, std::string> ParseDelegateAnnotations(const ::resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find("DELEGATE_POD_ANNOTATIONS");
    if (iter == instanceInfo.createoptions().end()) {
        return {};
    }
    return ParseMapFromStr(iter->second);
}

void ParseDelegateVolumeMounts(const ::resources::InstanceInfo &instanceInfo, const std::string optionKey,
                               std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts)
{
    auto iter = instanceInfo.createoptions().find(optionKey);
    if (iter == instanceInfo.createoptions().end()) {
        return;
    }
    ParseVolumeMountsFromJson(iter->second, volumeMounts);
}

void ParseVolumeMountsFromJson(const std::string jsonStr, std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts)
{
    if (jsonStr.empty()) {
        YRLOG_WARN("volume mount json str is empty.");
        return;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::json::parse_error &) {
        YRLOG_ERROR("error to parse volume mount.");
        return;
    }
    ParseVolumeMounts(json, volumeMounts);
}

bool IsSystemFunction(const ::resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find(RESOURCE_OWNER_KEY);
    return iter != instanceInfo.createoptions().end() && iter->second == SYSTEM_OWNER_VALUE;
}

bool IsNeedBindCPU(const std::map<std::string, std::string> &resourcesMap)
{
    auto iter = resourcesMap.find(CPU_RESOURCE);
    return iter != resourcesMap.end() && BIND_CPU_VALUE_SET.find(iter->second) != BIND_CPU_VALUE_SET.end();
}

std::string ParseNodeInternalIP(const std::shared_ptr<V1Node> &node)
{
    if (node == nullptr || node->GetStatus() == nullptr) {
        YRLOG_WARN("failed to parse node internal ip, node or node status is nullptr");
        return "";
    }
    auto addresses = node->GetStatus()->GetAddresses();
    // each node has at least two address(InternalIP/Hostname)
    for (const auto &address : addresses) {
        if (address == nullptr) {
            continue;
        }
        if (address->GetType() == INTERNAL_IP_TYPE) {
            return address->GetAddress();
        }
    }
    YRLOG_WARN("failed to parse node({}) internal ip", node->GetMetadata()->GetName());
    return "";
}

std::map<std::string, std::string> ParseMapFromStr(const std::string &val)
{
    std::map<std::string, std::string> result;
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(val);
    } catch (nlohmann::json::parse_error &e) {
        YRLOG_ERROR("failed to parse string to json, error: {}", e.what());
        return result;
    }
    for (const auto &item : parser.items()) {
        result[item.key()] = item.value();
    }
    return result;
}

std::map<std::string, std::string> ParseMapFromJson(const nlohmann::json &parser, const std::string &key)
{
    std::map<std::string, std::string> result;
    if (auto iter = parser.find(key); iter != parser.end() && iter->is_object()) {
        for (const auto &label : iter->items()) {
            result.emplace(label.key(), label.value());
        }
    }
    return result;
}

std::string ParseCPUEnv(std::string &cpuRes)
{
    if (cpuRes.find("m") != cpuRes.npos) {
        return litebus::strings::Trim(cpuRes, litebus::strings::Mode::ANY, "m");
    } else {
        try {
            auto cpuVal = std::stof(cpuRes);
            if (cpuVal <= 0 || (INT_MAX / cpuVal < CPU_SIZE_MILL)) {
                YRLOG_ERROR("invalid cpu value {}", cpuVal);
                return "";
            }
            return std::to_string(static_cast<int>(cpuVal * CPU_SIZE_MILL));
        } catch (const std::exception &e) {
            YRLOG_ERROR("fail to parse cpu value {}, error:{}", cpuRes, e.what());
            return "";
        }
    }
}

std::string ParseMemoryEnv(std::string &memRes)
{
    if (memRes.find(MEM_UNIT_MB) != memRes.npos) {
        return litebus::strings::Trim(memRes, litebus::strings::Mode::ANY, MEM_UNIT_MB);
    } else if (memRes.find(MEM_UNIT_GB) != memRes.npos) {
        try {
            auto memVal = std::stof(litebus::strings::Trim(memRes, litebus::strings::Mode::ANY, MEM_UNIT_GB));
            if (memVal <= 0 || (INT_MAX / memVal < MEM_SIZE_MB)) {
                YRLOG_ERROR("invalid memVal {}", memVal);
                return "";
            }
            return std::to_string(static_cast<int>(memVal * MEM_SIZE_MB));
        } catch (const std::exception &e) {
            YRLOG_ERROR("fail to parse cpu value, error:{}", e.what());
            return "";
        }
    } else {
        YRLOG_WARN("fail to parse mem value {}", memRes);
    }
    return "";
}

bool IsPodReady(const std::shared_ptr<V1Pod> &pod)
{
    if (!pod->StatusIsSet() || !pod->GetStatus()->ContainerStatusesIsSet()) {
        return false;
    }
    for (auto status : pod->GetStatus()->GetContainerStatuses()) {
        if (!status->ContainerIDIsSet()) {
            return false;
        }
    }
    // if pod is scaled by poolID, need to wait pod running (to improve the scheduling success rate.)
    if (pod->GetMetadata()->GetAnnotations().find(POD_POOL_ID) != pod->GetMetadata()->GetAnnotations().end() &&
        pod->GetStatus()->GetPhase() != PodPhase2StrMap.at(PodPhase::RUNNING)) {
        return false;
    }
    return true;
}

bool IsPodTerminating(const std::shared_ptr<V1Pod> &pod)
{
    if (!pod->StatusIsSet() || !pod->GetStatus()->ContainerStatusesIsSet()) {
        return false;
    }
    if (pod->GetStatus()->GetContainerStatuses().empty()) {
        return false;
    }
    auto &status = pod->GetStatus()->GetContainerStatuses().front();
    // only check one container
    if (!status->ContainerIDIsSet() || !status->StateIsSet()) {
        return false;
    }
    if (status->GetState()->TerminatedIsSet()) {
        return true;
    } else {
        return false;
    }
}

bool IsDsWorkerPodReady(const std::shared_ptr<V1Pod> &pod)
{
    if (!pod->StatusIsSet() || !pod->GetStatus()->ContainerStatusesIsSet()) {
        return false;
    }
    for (auto status : pod->GetStatus()->GetContainerStatuses()) {
        if (status->GetName() == "ds-worker" && status->IsReady()) {
            return true;
        }
    }
    return false;
}

bool IsPendingPod(const std::shared_ptr<V1Pod> &pod)
{
    if (pod->StatusIsSet() && pod->GetStatus()->PhaseIsSet()
        && (pod->GetStatus()->GetPhase() == PodPhase2StrMap.at(PodPhase::PENDING))) {
        return true;
    }
    return false;
}

bool IsAbnormalPod(const std::shared_ptr<V1Pod> &pod)
{
    if (pod->StatusIsSet() && pod->GetStatus()->PhaseIsSet()
        && (pod->GetStatus()->GetPhase() == PodPhase2StrMap.at(PodPhase::FAILED)
            || pod->GetStatus()->GetPhase() == PodPhase2StrMap.at(PodPhase::UNKNOWN))) {
        return true;
    }
    return false;
}

bool IsTimeoutPendingPod(const std::shared_ptr<V1Pod> &pod, const std::chrono::system_clock::time_point &receivedTime,
                         const int podPendingDurationThreshold)
{
    if (!(pod->StatusIsSet() && pod->GetStatus()->PhaseIsSet()
          && pod->GetStatus()->GetPhase() == PodPhase2StrMap.at(PodPhase::PENDING))) {
        return false;
    }
    long long pendingDuration = 0L;
    auto now = std::chrono::system_clock::now();
    pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(now - receivedTime).count();
    YRLOG_DEBUG("Time when the pending pod({}) was first received is {}, now is {}, Pending duration {}, threshold {}",
                pod->GetMetadata()->GetName(), receivedTime.time_since_epoch().count(),
                now.time_since_epoch().count(), pendingDuration, podPendingDurationThreshold);
    return pendingDuration >= podPendingDurationThreshold ? true : false;
}

void AddLabels(const ::resources::InstanceInfo &instanceInfo, std::map<std::string, std::string> &labels)
{
    if (auto iter = labels.find(REUSE_LABEL_KEY); iter == labels.end() || iter->second == "false") {
        labels[APP_LABEL_KEY] = TruncateIllegalLabel(instanceInfo.functionagentid());
    } else {
        labels[LABEL_SCHEDULE_POLICY] = MONOPOLY_SCHEDULE;
    }

    switch (instanceInfo.instancestatus().code()) {
        case static_cast<int32_t>(InstanceState::CREATING):
            // add label for affinity when instance state is creating
            for (const auto &label : instanceInfo.labels()) {
                if (labels.find(label) == labels.end()) {
                    auto items = litebus::strings::Split(label, ":");
                    if (items.size() > LABEL_SPLIT_ITEM_NUM) {
                        YRLOG_WARN("label key or value contains [:], label is {}", label);
                    }
                    labels[items[0]] = items.size() > 1 ? items[1] : "";
                }
            }
            break;
        case static_cast<int32_t>(InstanceState::RUNNING):
            for (const auto &label : GetLabelsFromCreateOpt(instanceInfo)) {
                labels[label.first] = label.second;
            }
            labels[INSTANCE_STATUS_LABEL_KEY] = "running";
            break;
        case static_cast<int32_t>(InstanceState::SUB_HEALTH):
            labels[INSTANCE_STATUS_LABEL_KEY] = "subHealth";
            break;
        case static_cast<int32_t>(InstanceState::EXITING):
            labels[GRACEFUL_SHUTDOWN_LABEL_KEY] = "true"; // compatible
            labels[INSTANCE_STATUS_LABEL_KEY] = "exiting";
            break;
        case static_cast<int32_t>(InstanceState::EVICTING):
            labels[GRACEFUL_SHUTDOWN_LABEL_KEY] = "true"; // compatible
            labels[INSTANCE_STATUS_LABEL_KEY] = "evicting";
            break;
        default:
            YRLOG_WARN("unknown instance status({}), while try to update pod labels",
                       instanceInfo.instancestatus().code());
    }
}

void HandlePodLabelsWithAnnotation(const ::resources::InstanceInfo &instanceInfo,
                                   const std::shared_ptr<V1Pod> &pod, bool isInstanceDelete)
{
    if (isInstanceDelete) {
        RemovePodLabelsByInstance(instanceInfo, pod);
        return;
    }
    if (instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING)) {
        AddPodLabelsByInstance(instanceInfo, pod);
    } else if (instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::FATAL) ||
               instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::SCHEDULE_FAILED)) {
        RemovePodLabelsByInstance(instanceInfo, pod);
    }
}

void AddPodLabelsByInstance(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Pod> &pod)
{
    auto delegateLabels = GetLabelsFromCreateOpt(instanceInfo);
    if (delegateLabels.empty()) {
        return;
    }
    for (const auto &label : delegateLabels) {
        if (label.first.empty()) {
            continue;
        }
        pod->GetMetadata()->GetLabels()[label.first] = label.second;
    }
    if (!pod->GetMetadata()->AnnotationsIsSet()) {
        pod->GetMetadata()->SetAnnotations({});
    }
    auto labelMapStr = instanceInfo.createoptions().at(DELEGATE_POD_LABELS);
    pod->GetMetadata()->GetAnnotations()[ANNOTATION_KEY_PREFIX + instanceInfo.instanceid()] = labelMapStr;
}

void RemovePodLabelsByInstance(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Pod> &pod)
{
    auto delegateLabels = GetLabelsFromCreateOpt(instanceInfo);
    if (delegateLabels.empty()) {
        return;
    }
    auto key = ANNOTATION_KEY_PREFIX + instanceInfo.instanceid();
    if (pod->GetMetadata()->GetAnnotations().find(key) == pod->GetMetadata()->GetAnnotations().end()) {
        return;
    }
    for (const auto &label : delegateLabels) {
        pod->GetMetadata()->GetLabels().erase(label.first);
    }
    pod->GetMetadata()->GetAnnotations().erase(ANNOTATION_KEY_PREFIX + instanceInfo.instanceid());
}

void RemovePodLabelsByAnnotations(const std::string instanceID, const std::shared_ptr<V1Pod> &pod)
{
    auto annoKey = ANNOTATION_KEY_PREFIX + instanceID;
    auto annotations = pod->GetMetadata()->GetAnnotations();
    if (annotations.find(annoKey) == annotations.end()) {
        return;
    }
    auto delegateLabels = ParseMapFromStr(annotations[annoKey]);
    for (const auto &label : delegateLabels) {
        pod->GetMetadata()->GetLabels().erase(label.first);
    }
    pod->GetMetadata()->GetAnnotations().erase(annoKey);
}

std::shared_ptr<V2HorizontalPodAutoscalerSpec> ParseHorizontalPodAutoscaler(const std::string &jsonStr)
{
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::json::parse_error &e) {
        YRLOG_ERROR("failed to parse string to json, error: {}", e.what());
        return nullptr;
    }
    auto spec = std::make_shared<V2HorizontalPodAutoscalerSpec>();
    if (spec->FromJson(parser)) {
        return spec;
    }
    return nullptr;
}

void AddAffinityForPodSpec(const std::shared_ptr<V1Affinity> affinity, const std::shared_ptr<V1PodTemplateSpec> &spec,
                           const bool isAggregation)
{
    if (affinity == nullptr) {
        return;
    }
    if (!spec->GetSpec()->AffinityIsSet()) {
        spec->GetSpec()->SetAffinity(std::make_shared<V1Affinity>());
    }
    if (affinity->PodAntiAffinityIsSet()) {
        spec->GetSpec()->GetAffinity()->SetPodAntiAffinity(affinity->GetPodAntiAffinity());
    }
    if (affinity->PodAffinityIsSet()) {
        spec->GetSpec()->GetAffinity()->SetPodAffinity(affinity->GetPodAffinity());
    }
    if (affinity->NodeAffinityIsSet()) {
        spec->GetSpec()->GetAffinity()->SetNodeAffinity(MergeNodeAffinity(
            spec->GetSpec()->GetAffinity()->GetNodeAffinity(), affinity->GetNodeAffinity(), isAggregation));
    }
}

bool IsAggregationMergePolicy(const ::resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find(DELEGATE_NODE_AFFINITY_POLICY);
    if (iter == instanceInfo.createoptions().end()) {
        return false;
    }
    return iter->second == NODE_AFFINITY_POLICY_AGGREGATION;
}

std::shared_ptr<V1NodeAffinity> MergeNodeAffinity(const std::shared_ptr<V1NodeAffinity> &oldAff,
                                                  const std::shared_ptr<V1NodeAffinity> &newAff,
                                                  const bool isAggregation)
{
    if (newAff == nullptr) {
        return oldAff;
    }
    if (oldAff == nullptr) {
        return newAff;
    }
    auto nodeAffinity = std::make_shared<V1NodeAffinity>();

    if (oldAff->PreferredDuringSchedulingIgnoredDuringExecutionIsSet()) {
        nodeAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution(
            oldAff->GetPreferredDuringSchedulingIgnoredDuringExecution());
    }
    if (oldAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet()) {
        nodeAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(
            oldAff->GetRequiredDuringSchedulingIgnoredDuringExecution());
    }
    if (!isAggregation) {
        // means new affinity coverage old
        if (newAff->PreferredDuringSchedulingIgnoredDuringExecutionIsSet()) {
            nodeAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution(
                newAff->GetPreferredDuringSchedulingIgnoredDuringExecution());
        }
        if (newAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet()) {
            nodeAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(
                newAff->GetRequiredDuringSchedulingIgnoredDuringExecution());
        }
        return nodeAffinity;
    }
    // aggregation affinity
    std::vector<std::shared_ptr<kube_client::model::V1PreferredSchedulingTerm>> preferExecutions{};
    for (auto item : oldAff->GetPreferredDuringSchedulingIgnoredDuringExecution()) {
        preferExecutions.emplace_back(item);
    }
    for (auto item : newAff->GetPreferredDuringSchedulingIgnoredDuringExecution()) {
        preferExecutions.emplace_back(item);
    }
    nodeAffinity->SetPreferredDuringSchedulingIgnoredDuringExecution(preferExecutions);
    if (!newAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet()) {
        return nodeAffinity;
    } else if (!oldAff->RequiredDuringSchedulingIgnoredDuringExecutionIsSet()) {
        // if old is not set, new set, just to coverage
        nodeAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(
            newAff->GetRequiredDuringSchedulingIgnoredDuringExecution());
        return nodeAffinity;
    }
    auto nodeSelector = AggregationNodeSelector(oldAff->GetRequiredDuringSchedulingIgnoredDuringExecution(),
                                                newAff->GetRequiredDuringSchedulingIgnoredDuringExecution());
    if (nodeSelector != nullptr) {
        nodeAffinity->SetRequiredDuringSchedulingIgnoredDuringExecution(nodeSelector);
    }
    return nodeAffinity;
}

std::shared_ptr<kube_client::model::V1NodeSelector> AggregationNodeSelector(
    const std::shared_ptr<kube_client::model::V1NodeSelector> &oldSel,
    const std::shared_ptr<kube_client::model::V1NodeSelector> &newSel)
{
    if (oldSel == nullptr || oldSel->GetNodeSelectorTerms().empty()) {
        return newSel;
    }
    if (newSel == nullptr || newSel->GetNodeSelectorTerms().empty()) {
        return oldSel;
    }
    auto nodeSelector = std::make_shared<kube_client::model::V1NodeSelector>();
    std::vector<std::shared_ptr<kube_client::model::V1NodeSelectorTerm>> nodeSelectorTerms{};
    auto srcLen = static_cast<int>(oldSel->GetNodeSelectorTerms().size() > NODE_AFFINITY_MAX_LEN
                                       ? NODE_AFFINITY_MAX_LEN
                                       : oldSel->GetNodeSelectorTerms().size());
    auto destLen = static_cast<int>(newSel->GetNodeSelectorTerms().size() > NODE_AFFINITY_MAX_LEN
                                        ? NODE_AFFINITY_MAX_LEN
                                        : newSel->GetNodeSelectorTerms().size());
    for (int i = 0; i < srcLen; i++) {
        for (int j = 0; j < destLen; j++) {
            auto item = std::make_shared<kube_client::model::V1NodeSelectorTerm>();
            std::vector<std::shared_ptr<kube_client::model::V1NodeSelectorRequirement>> matchExpressions{};
            std::vector<std::shared_ptr<kube_client::model::V1NodeSelectorRequirement>> matchFields{};
            auto srcTerms = oldSel->GetNodeSelectorTerms()[i];
            auto destTerms = newSel->GetNodeSelectorTerms()[j];
            for (auto it : srcTerms->GetMatchExpressions()) {
                matchExpressions.emplace_back(it);
            }
            for (auto it : destTerms->GetMatchExpressions()) {
                matchExpressions.emplace_back(it);
            }
            for (auto it : srcTerms->GetMatchFields()) {
                matchFields.emplace_back(it);
            }
            for (auto it : destTerms->GetMatchFields()) {
                matchFields.emplace_back(it);
            }
            item->SetMatchExpressions(matchExpressions);
            item->SetMatchFields(matchFields);
            nodeSelectorTerms.emplace_back(item);
        }
    }
    nodeSelector->SetNodeSelectorTerms(nodeSelectorTerms);
    return nodeSelector;
}

std::string TruncateIllegalLabel(const std::string& label)
{
    if (label.length() <= MAX_LABEL_LEN) {
        return label;
    }
    return label.substr(label.length() - MAX_LABEL_LEN);
}
}  // namespace functionsystem