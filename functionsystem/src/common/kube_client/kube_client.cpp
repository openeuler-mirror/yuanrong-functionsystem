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

#include "kube_client.h"

#include "common/status/status.h"
#include "utils/os_utils.hpp"
#include "async/async.hpp"

namespace functionsystem {

SslConfig KubeClient::ClusterSslConfig(const std::string &clientCertFile, const std::string &clientKeyFile,
                                       const bool isSkipTlsVerify)
{
    // The following scenarios are supported:
    // Scenario 1: Only the root certificate is configured.
    // Scenario 2: Only the client certificate and root certificate are configured.
    // Scenario 3: Configuring the Password (Plaintext or Ciphertext) for Encrypting the Key of the Personal
    // Certificate, Client Certificate, and Root Certificate
    SslConfig sslConfig{
        .clientCertFile = "", .clientKeyFile = "", .caCertFile = "", .credential = "k8sClient", .isSkipTlsVerify = false
    };

    if (!clientCertFile.empty()) {
        sslConfig.clientCertFile = clientCertFile;
        if (!litebus::os::ExistPath(clientCertFile)) {
            YRLOG_ERROR("kubernetes client cert not exist.");
        }
    }

    if (!clientKeyFile.empty()) {
        sslConfig.clientKeyFile = clientKeyFile;
        if (!litebus::os::ExistPath(clientKeyFile)) {
            YRLOG_ERROR("kubernetes client key not exist.");
        }
    }

    if (litebus::os::ExistPath(SERVICE_CERT_FILENAME)) {
        // If there are multiple root certificates, you need to configure the root certificate directory.
        // During handshake, the server returns a Hello message carrying the server public key certificate. The client
        // verifies the validity of the server public key certificate based on the CA.
        sslConfig.caCertFile = SERVICE_CERT_FILENAME;
    } else {
        YRLOG_ERROR("kubernetes service account's ca.crt not exist.");
    }
    sslConfig.isSkipTlsVerify = isSkipTlsVerify;  // default false
    return sslConfig;
}

std::shared_ptr<KubeClient> KubeClient::CreateKubeClient(const std::string &basePath, const SslConfig &sslConfig,
                                                         bool startMonitor, const HealthMonitorParam &param)
{
    auto kubeClient = std::make_shared<KubeClient>();
    auto apiConf = std::make_shared<ApiConfiguration>();
    apiConf->SetBaseUrl(basePath);
    apiConf->SetSslConfig(sslConfig);

    auto token = litebus::os::Read(SERVICE_TOKEN_FILENAME);
    if (token.IsSome() && !token.Get().empty()) {
        // "Authorization": "Bearer *************"
        apiConf->SetApiKey("Authorization", token.Get());
    } else {
        YRLOG_ERROR("token file not exist or failed to read token from pod!!!");
    }

    auto k8sClient = std::make_shared<ApiClient>(apiConf);
    k8sClient->SetResponseHandler(
        [](litebus::http::ResponseCode resCode, const litebus::http::HeaderMap & /* header */) {
            if (resCode != litebus::http::ResponseCode::OK) {
                YRLOG_WARN("KubeClient resCode: {}", fmt::underlying(resCode));
            }
        });
    kubeClient->SetK8sClient(k8sClient);
    kubeClient->SetK8sClientApi(std::make_shared<K8sClientApi>(k8sClient));
    kubeClient->SetCoreClientApi(std::make_shared<CoreClientApi>(k8sClient));
    kubeClient->SetCoordinationApi(std::make_shared<CoordinationV1Api>(k8sClient));
    kubeClient->SetAutoscalingV2Api(std::make_shared<AutoscalingV2Api>(k8sClient));

    if (startMonitor) {
        auto healthMonitor = std::make_shared<HealthMonitor>(param);
        healthMonitor->SubscribeK8sHealth([=](bool res) {
            kubeClient->OnK8sHealthStatusChangeHandler(res);
        });
        healthMonitor->RegisterHandler([kubeClient]() {
            return kubeClient->coreClientApi_->ListForMonitor();
        });
        kubeClient->SetHealthMonitor(healthMonitor);
        litebus::Spawn(healthMonitor);
    }
    return kubeClient;
}

void KubeClient::InitOwnerReference(const std::string &rNamespace, const std::string &ownerName)
{
    ownerReference_ = std::make_shared<V1OwnerReference>();
    ownerReference_->SetName(ownerName);
    ownerReference_->SetController(true);
    ownerReference_->SetBlockOwnerDeletion(true);
    ownerReference_->SetKind("Deployment");
    ownerReference_->SetApiVersion("apps/v1");
    ownerReference_->SetUid("");
}

bool KubeClient::RegisterK8sStatusChangeHandler(const K8sStatusChangeEvent &eventType,
                                                const std::string &subscriberName,
                                                const std::function<void()> &handler)
{
    if (eventType >= K8sStatusChangeEvent::ON_MAX) {
        YRLOG_WARN("eventType is invalid, eventType:{}", static_cast<int>(eventType));
        return false;
    }
    k8sStatusChangeHandlerMap[eventType][subscriberName] = handler;
    return true;
}

void KubeClient::OnK8sHealthStatusChangeHandler(bool healthy)
{
    YRLOG_WARN("K8s health status changes to: {}", healthy);
    SetIsK8sHealthy(healthy);
    // when new status is healthy, it means k8s recovered. Call OnRecoverHandler
    if (healthy) {
        ExecuteHandler(K8sStatusChangeEvent::ON_RECOVER);
    } else {  // when new status is unhealthy, it means k8s on failure. Call OnFailHandler
        ExecuteHandler(K8sStatusChangeEvent::ON_FAIL);
    }
}

void KubeClient::ExecuteHandler(const K8sStatusChangeEvent &eventType)
{
    static std::map<K8sStatusChangeEvent, std::string> k8sStatusChangeMap = {
        { K8sStatusChangeEvent::ON_RECOVER, "ON_RECOVER" }, { K8sStatusChangeEvent::ON_FAIL, "ON_FAIL" }
    };
    auto onRecoverHandlerMap = k8sStatusChangeHandlerMap.find(eventType);
    if (onRecoverHandlerMap != k8sStatusChangeHandlerMap.end()) {
        YRLOG_INFO("There are {} {} callbacks", onRecoverHandlerMap->second.size(),
                   k8sStatusChangeMap.find(eventType)->second);
        for (auto &handler : onRecoverHandlerMap->second) {
            handler.second();
        }
    }
}

KubeClient::~KubeClient()
{
    if (healthMonitor_ != nullptr) {
        litebus::Terminate(healthMonitor_->GetAID());
        litebus::Await(healthMonitor_->GetAID());
    }
}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> KubeClient::ListNamespacedHorizontalPodAutoscaler(
    const std::string &rNamespace, const litebus::Option<bool> &watch)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscalerList>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscalerList>>>();
    auto callback = [=] {
        return autoscalingV2Api_->ListNamespacedHorizontalPodAutoscaler(
            rNamespace, litebus::None(), litebus::None(), watch);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> KubeClient::CreateNamespacedHorizontalPodAutoscaler(
    const std::string &rNamespace, const std::shared_ptr<V2HorizontalPodAutoscaler> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>>>();
    auto callback = [=] {
        return autoscalingV2Api_->CreateNamespacedHorizontalPodAutoscaler(rNamespace, body);
    };
    return CreateWithOwnerReference(rNamespace, body, promise, callback);
}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> KubeClient::PatchNamespacedHorizontalPodAutoscaler(
    const std::string &name,
    const std::string &rNamespace,
    const std::shared_ptr<Object> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>>>();
    auto callback = [=] {
        return autoscalingV2Api_->PatchNamespacedHorizontalPodAutoscaler(name, rNamespace, body);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Status>> KubeClient::DeleteNamespacedHorizontalPodAutoscaler(
    const std::string &name,
    const std::string &rNamespace,
    const litebus::Option<bool> &orphanDependents,
    const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Status>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Status>>>();
    auto callback = [=] {
        return autoscalingV2Api_->DeleteNamespacedHorizontalPodAutoscaler(name, rNamespace);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1DeploymentList>> KubeClient::ListNamespacedDeployment(
    const std::string &rNamespace, const litebus::Option<bool> &watch)
{
    if (watch.IsSome()) {
        return DoListNamespaceDeployment(rNamespace, litebus::None(), litebus::None(), watch);
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1DeploymentList>>>();
    std::shared_ptr<V1DeploymentList> result = std::make_shared<V1DeploymentList>();
    result->SetItems({});
    ListDeploymentByPage(result, promise, rNamespace, litebus::None(), pageLimit_);
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<V1DeploymentList>> KubeClient::DoListNamespaceDeployment(
    const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
    const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1DeploymentList>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1DeploymentList>>>();
    auto callback = [=] {
        return k8sClientApi_->ListNamespacedDeployment(rNamespace, rContinue, limit, watch);
    };
    return ClientResponseHandler(promise, 1, callback);
}

void KubeClient::ListDeploymentByPage(
    const std::shared_ptr<V1DeploymentList> &deployList,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<V1DeploymentList>>> &promise, const std::string &rNamespace,
    const litebus::Option<std::string> &rContinue, const litebus::Option<uint32_t> &limit)
{
    DoListNamespaceDeployment(rNamespace, rContinue, limit, false)
        .OnComplete([=](const litebus::Future<std::shared_ptr<V1DeploymentList>> res) {
            if (res.IsError()) {
                YRLOG_WARN("failed to list deployment, code({})", res.GetErrorCode());
                promise->SetValue(std::move(deployList));
                return;
            }
            auto deployListRes = res.Get();
            std::string nContinue = deployListRes->GetMetadata()->GetRContinue();
            auto cnt = deployListRes->GetItems().size();
            if (cnt > 0) {
                deployList->GetItems().insert(deployList->GetItems().end(), deployListRes->GetItems().begin(),
                                              deployListRes->GetItems().end());
            }
            if (cnt < pageLimit_ || nContinue.empty()) {
                promise->SetValue(std::move(deployList));
            } else {
                ListDeploymentByPage(deployList, promise, rNamespace, nContinue, pageLimit_);
            }
        });
}

litebus::Future<std::shared_ptr<V1Deployment>> KubeClient::CreateNamespacedDeployment(
    const std::string &rNamespace, const std::shared_ptr<V1Deployment> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
    auto callback = [=] {
        return k8sClientApi_->CreateNamespacedDeployment(rNamespace, body);
    };
    return CreateWithOwnerReference(rNamespace, body, promise, callback);
}

litebus::Future<std::shared_ptr<V1Deployment>> KubeClient::DeleteNamespacedDeployment(const std::string &name,
                                                                                      const std::string &rNamespace)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
    auto callback = [=] {
        return k8sClientApi_->DeleteNamespacedDeployment(name, rNamespace);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Deployment>> KubeClient::PatchNamespacedDeployment(
    const std::string &name, const std::string &rNamespace, const std::shared_ptr<Object> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
    auto callback = [=] {
        return k8sClientApi_->PatchNamespacedDeployment(name, rNamespace, body);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Deployment>> KubeClient::ReadNamespacedDeployment(const std::string &name,
                                                                                    const std::string &rNamespace)
{
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Deployment>>>();
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto callback = [=] { return k8sClientApi_->ReadNamespacedDeployment(name, rNamespace); };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1PodList>> KubeClient::ListNamespacedPod(const std::string &rNamespace,
                                                                          const litebus::Option<bool> &watch)
{
    if (watch.IsSome()) {
        return DoListNamespacePod(rNamespace, litebus::None(), litebus::None(), watch);
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1PodList>>>();
    std::shared_ptr<V1PodList> result = std::make_shared<V1PodList>();
    result->SetItems({});
    ListNamespacedPodByPage(result, promise, rNamespace, litebus::None(), pageLimit_);
    return promise->GetFuture();
}

void KubeClient::ListNamespacedPodByPage(const std::shared_ptr<V1PodList> &podList,
                                         const std::shared_ptr<litebus::Promise<std::shared_ptr<V1PodList>>> &promise,
                                         const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
                                         const litebus::Option<uint32_t> &limit)
{
    DoListNamespacePod(rNamespace, rContinue, limit, false)
        .OnComplete([=](const litebus::Future<std::shared_ptr<V1PodList>> res) {
            if (res.IsError()) {
                YRLOG_WARN("failed to list pod, code({})", res.GetErrorCode());
                promise->SetValue(std::move(podList));
                return;
            }
            auto podListRes = res.Get();
            std::string nContinue = podListRes->GetMetadata()->GetRContinue();
            auto cnt = podListRes->GetItems().size();
            if (cnt > 0) {
                podList->GetItems().insert(podList->GetItems().end(), podListRes->GetItems().begin(),
                                           podListRes->GetItems().end());
            }
            if (cnt < pageLimit_ || nContinue.empty()) {
                promise->SetValue(std::move(podList));
            } else {
                YRLOG_DEBUG("start to list next page {}", nContinue);
                ListNamespacedPodByPage(podList, promise, rNamespace, nContinue, pageLimit_);
            }
        });
}

litebus::Future<std::shared_ptr<V1PodList>> KubeClient::DoListNamespacePod(
    const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
    const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1PodList>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1PodList>>>();
    auto callback = [=] {
        return coreClientApi_->ListNamespacedPod(rNamespace, rContinue, limit, watch);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Pod>> KubeClient::CreateNamespacedPod(const std::string &rNamespace,
                                                                        const std::shared_ptr<V1Pod> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
    auto callback = [=] { return coreClientApi_->CreateNamespacedPod(rNamespace, body); };
    return CreateWithOwnerReference(rNamespace, body, promise, callback);
}

litebus::Future<std::shared_ptr<V1Pod>> KubeClient::PatchNamespacedPod(const std::string &name,
                                                                       const std::string &rNamespace,
                                                                       const std::shared_ptr<Object> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
    auto callback = [=] { return coreClientApi_->PatchNamespacedPod(name, rNamespace, body); };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Pod>> KubeClient::DeleteNamespacedPod(
    const std::string &name, const std::string &rNamespace,
    const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body, const litebus::Option<bool> &orphanDependents)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
    auto callback = [=] {
        return coreClientApi_->DeleteNamespacedPod(name, rNamespace, orphanDependents, body);
    };
    return ClientResponseHandler(promise, 1, callback);
}

litebus::Future<std::shared_ptr<V1Pod>> KubeClient::ReadNamespacedPod(const std::string &rNamespace,
                                                                      const std::string &name)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Pod>>>();
    auto callback = [coreClientApi(coreClientApi_), rNamespace, name] {
        return coreClientApi->ReadNamespacedPod(rNamespace, name);
    };
    return ClientResponseHandler(promise, 1, callback, false, true);
}

litebus::Future<std::shared_ptr<V1NodeList>> KubeClient::ListNode(const litebus::Option<bool> &watch)
{
    if (watch.IsSome()) {
        return DoListNode(litebus::None(), litebus::None(), watch);
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1NodeList>>>();
    std::shared_ptr<V1NodeList> result = std::make_shared<V1NodeList>();
    result->SetItems({});
    ListNodeByPage(result, promise, litebus::None(), pageLimit_);
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<V1NodeList>> KubeClient::DoListNode(const litebus::Option<std::string> &rContinue,
                                                                    const litebus::Option<uint32_t> &limit,
                                                                    const litebus::Option<bool> &watch)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1NodeList>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1NodeList>>>();
    auto callback = [=] {
        return coreClientApi_->ListNode(rContinue, limit, watch);
    };
    return ClientResponseHandler(promise, 1, callback);
}

void KubeClient::ListNodeByPage(const std::shared_ptr<V1NodeList> &nodeList,
                                const std::shared_ptr<litebus::Promise<std::shared_ptr<V1NodeList>>> &promise,
                                const litebus::Option<std::string> &rContinue,
                                const litebus::Option<uint32_t> &limit)
{
    DoListNode(rContinue, limit, false).OnComplete([=](const litebus::Future<std::shared_ptr<V1NodeList>> res) {
        if (res.IsError()) {
            YRLOG_WARN("failed to list node, code({})", res.GetErrorCode());
            promise->SetValue(std::move(nodeList));
            return;
        }
        auto nodeListRes = res.Get();
        std::string nContinue = nodeListRes->GetMetadata()->GetRContinue();
        auto cnt = nodeListRes->GetItems().size();
        if (cnt > 0) {
            nodeList->GetItems().insert(nodeList->GetItems().end(), nodeListRes->GetItems().begin(),
                                        nodeListRes->GetItems().end());
        }
        if (cnt < pageLimit_ || nContinue.empty()) {
            promise->SetValue(std::move(nodeList));
        } else {
            YRLOG_DEBUG("start to list next page {}", nContinue);
            ListNodeByPage(nodeList, promise, nContinue, pageLimit_);
        }
    });
}

litebus::Future<std::shared_ptr<V1Node>> KubeClient::PatchNode(const std::string &name,
                                                               const std::shared_ptr<Object> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Node>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Node>>>();
    auto callback = [=] { return coreClientApi_->PatchNode(name, body); };
    return ClientResponseHandler(promise, 1, callback);
}

bool KubeClient::RegisterWatchHandler(const std::string &modelName, const K8sEventType &eventType,
                                      const WatchHandler &handler)
{
    return k8sClient_->RegisterWatchHandler(modelName, eventType, handler);
}

litebus::Future<std::shared_ptr<V1Lease>> KubeClient::CreateNamespacedLease(const std::string &rNamespace,
                                                                            const std::shared_ptr<V1Lease> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    auto callback = [=] { return coordinationV1Api_->CreateNamespacedLease(rNamespace, body); };
    return CreateWithOwnerReference(rNamespace, body, promise, callback);
}

litebus::Future<std::shared_ptr<V1Lease>> KubeClient::ReadNamespacedLease(const std::string &name,
                                                                          const std::string &rNamespace)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    auto callback = [=] { return coordinationV1Api_->ReadNamespacedLease(name, rNamespace); };
    return ClientResponseHandler(promise, 1, callback, false, true);
}

litebus::Future<std::shared_ptr<V1Lease>> KubeClient::ReplaceNamespacedLease(const std::string &name,
                                                                             const std::string &rNamespace,
                                                                             const std::shared_ptr<V1Lease> &body)
{
    if (!isK8sHealthy) {
        YRLOG_ERROR("K8s health status is not ok, return err directly.");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
        promise->SetFailed(static_cast<int32_t>(StatusCode::ERR_K8S_UNAVAILABLE));
        return promise->GetFuture();
    }
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    auto callback = [=] { return coordinationV1Api_->ReplaceNamespacedLease(name, rNamespace, body); };
    return ClientResponseHandler(promise, 1, callback, false, true);
}

template <typename T, typename Func>
litebus::Future<T> KubeClient::ClientResponseHandler(const std::shared_ptr<litebus::Promise<T>> &promise,
                                                     const uint32_t curRetryTime, const Func &callback,
                                                     const bool &retry, const bool &monitor)
{
    auto timeout = retryCycMs_ * (std::pow(2, (curRetryTime - 1))) +
                   GenerateRandomNumber(static_cast<uint32_t>(0), retryCycMsUpper_);
    callback().OnComplete([=](const litebus::Future<T> &future) {
        if (future.IsOK()) {
            promise->SetValue(future);
            return;
        }
        // do monitor
        if (monitor && healthMonitor_ != nullptr && IsErrorResponse(future.GetErrorCode(), false)) {
            litebus::Async(healthMonitor_->GetAID(), &HealthMonitor::Start);
        }
        // do retry(promise)
        if (retry && curRetryTime <= retryTime_ && IsErrorResponse(future.GetErrorCode(), true)) {
            promise->GetFuture().After(timeout, [=](const litebus::Future<T> &x) {
                return ClientResponseHandler(promise, curRetryTime + 1, callback, retry, monitor);
            });
            return;
        }
        YRLOG_DEBUG(
            "ClientResponseHandler OnComplete Failure: maxRetry is {}, cycTime is {}, retry time is {}, timeout is {}ms"
            ", code is {}, retry is {}, monitor is {}",
            retryTime_, retryCycMs_, (curRetryTime - 1), timeout, future.GetErrorCode(), retry, monitor);
        promise->SetFailed(future.GetErrorCode());
    });
    return promise->GetFuture();
}

template <typename T, typename Func>
litebus::Future<T> KubeClient::CreateWithOwnerReference(const std::string rNamespace, const T &body,
                                                        const std::shared_ptr<litebus::Promise<T>> &promise,
                                                        const Func &callback)
{
    if (ownerReference_ == nullptr || !body->GetMetadata()->GetOwnerReferences().empty()) {
        return ClientResponseHandler(promise, 1, callback);
    }
    if (!ownerReference_->GetUid().empty()) {
        body->GetMetadata()->SetOwnerReferences({ ownerReference_ });
        return ClientResponseHandler(promise, 1, callback);
    }
    ReadNamespacedDeployment(ownerReference_->GetName(), rNamespace)
        .OnComplete([=](const litebus::Future<std::shared_ptr<V1Deployment>> &deployment) {
            if (deployment.IsError()) {
                YRLOG_ERROR("failed to query owner reference error is {}", deployment.GetErrorCode());
                promise->SetFailed(deployment.GetErrorCode());
                return;
            }
            ownerReference_->SetUid(deployment.Get()->GetMetadata()->GetUid());
            body->GetMetadata()->SetOwnerReferences({ ownerReference_ });
            ClientResponseHandler(promise, 1, callback);
        });
    return promise->GetFuture();
}

bool KubeClient::IsErrorResponse(int code, const bool &retry)
{
    bool res = code >= litebus::http::ResponseCode::INTERNAL_SERVER_ERROR ||
    code == litebus::http::HttpErrorCode::CONNECTION_REFUSED ||
    code == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT;
    if (retry) {
        res = res || (code == litebus::http::ResponseCode::TOO_MANY_REQUESTS);
    }
    return res;
}

}  // namespace functionsystem