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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_H

#include <string>
#include <utility>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/random_number.h"
#include "common/kube_client/api/apps_v1_api.h"
#include "common/kube_client/api/coordination_v1_api.h"
#include "common/kube_client/api/core_v1_api.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/health_monitor/health_monitor.h"
#include "common/kube_client/model/horizontal_pod_autoscaler/v2_horizontal_pod_autoscaler_list.h"
#include "common/kube_client/api/autoscaling_v2_api.h"
#include "common/kube_client/model/common/v1_status.h"

namespace functionsystem {

const std::string SERVICE_CERT_FILENAME = "/var/run/secrets/kubernetes.io/serviceaccount/ca.crt";  // NOLINT
const std::string SERVICE_TOKEN_FILENAME = "/var/run/secrets/kubernetes.io/serviceaccount/token";  // NOLINT
const uint32_t DEFAULT_LIMIT = 100;
const uint32_t RETRY_CYC_RANDOM_UPPER = 1000;

using ApiClient = functionsystem::kube_client::api::ApiClient;
using K8sClientApi = functionsystem::kube_client::api::AppsV1Api;
using CoreClientApi = functionsystem::kube_client::api::CoreV1Api;
using CoordinationV1Api = functionsystem::kube_client::api::CoordinationV1Api;
using ApiConfiguration = functionsystem::kube_client::api::ApiConfiguration;
using SslConfig = functionsystem::kube_client::api::SslConfig;
using V1Deployment = functionsystem::kube_client::model::V1Deployment;
using V1DeploymentList = functionsystem::kube_client::model::V1DeploymentList;
using V1PodList = functionsystem::kube_client::model::V1PodList;
using V1Pod = functionsystem::kube_client::model::V1Pod;
using V1NodeList = functionsystem::kube_client::model::V1NodeList;
using V1Node = functionsystem::kube_client::model::V1Node;
using V1DeleteOptions = functionsystem::kube_client::model::V1DeleteOptions;
using Object = functionsystem::kube_client::model::Object;
using V1Lease = functionsystem::kube_client::model::V1Lease;
using WatchHandler = functionsystem::kube_client::api::WatchHandler;
using K8sEventType = functionsystem::kube_client::api::EventType;
using V2HorizontalPodAutoscaler = functionsystem::kube_client::model::V2HorizontalPodAutoscaler;
using V2HorizontalPodAutoscalerList = functionsystem::kube_client::model::V2HorizontalPodAutoscalerList;
using AutoscalingV2Api = functionsystem::kube_client::api::AutoscalingV2Api;
using V1Status = functionsystem::kube_client::model::V1Status;
using V1OwnerReference = functionsystem::kube_client::api::V1OwnerReference;

enum class K8sStatusChangeEvent { ON_FAIL = 0, ON_RECOVER = 1, ON_MAX = 2 };

class KubeClient {
public:
    KubeClient() = default;
    virtual ~KubeClient();

    static SslConfig ClusterSslConfig(const std::string &clientCertFile, const std::string &clientKeyFile,
                                      const bool isSkipTlsVerify);

    static std::shared_ptr<KubeClient> CreateKubeClient(const std::string &basePath, const SslConfig &sslConfig,
                                                        bool startMonitor = false,
                                                        const HealthMonitorParam &param = HealthMonitorParam());
    virtual void InitOwnerReference(const std::string &rNamespace, const std::string &ownerName);

    virtual bool RegisterK8sStatusChangeHandler(const K8sStatusChangeEvent &eventType,
                                                const std::string &subscriberName,
                                                const std::function<void()> &handler);

    virtual litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> ListNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace, const litebus::Option<bool> &watch = false);

    virtual litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> CreateNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace, const std::shared_ptr<V2HorizontalPodAutoscaler> &body);

    virtual litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> PatchNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace,
        const std::shared_ptr<Object> &body);

    virtual litebus::Future<std::shared_ptr<V1Status>> DeleteNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace,
        const litebus::Option<bool> &orphanDependents,
        const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body);

    virtual litebus::Future<std::shared_ptr<V1DeploymentList>> ListNamespacedDeployment(
        const std::string &rNamespace, const litebus::Option<bool> &watch = litebus::None());

    virtual litebus::Future<std::shared_ptr<V1Deployment>> CreateNamespacedDeployment(
        const std::string &rNamespace, const std::shared_ptr<V1Deployment> &body);

    virtual litebus::Future<std::shared_ptr<V1Deployment>> DeleteNamespacedDeployment(const std::string &name,
                                                                                      const std::string &rNamespace);

    virtual litebus::Future<std::shared_ptr<V1Deployment>> PatchNamespacedDeployment(
        const std::string &name, const std::string &rNamespace, const std::shared_ptr<Object> &body);

    virtual litebus::Future<std::shared_ptr<V1Deployment>> ReadNamespacedDeployment(const std::string &name,
                                                                                    const std::string &rNamespace);

    virtual litebus::Future<std::shared_ptr<V1PodList>> ListNamespacedPod(
        const std::string &rNamespace, const litebus::Option<bool> &watch = litebus::None());

    virtual litebus::Future<std::shared_ptr<V1Pod>> CreateNamespacedPod(const std::string &rNamespace,
                                                                        const std::shared_ptr<V1Pod> &body);

    virtual litebus::Future<std::shared_ptr<V1Pod>> PatchNamespacedPod(const std::string &name,
                                                                       const std::string &rNamespace,
                                                                       const std::shared_ptr<Object> &body);

    virtual litebus::Future<std::shared_ptr<V1Pod>> DeleteNamespacedPod(
        const std::string &name, const std::string &rNamespace,
        const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body = litebus::None(),
        const litebus::Option<bool> &orphanDependents = litebus::None());

    virtual litebus::Future<std::shared_ptr<V1Pod>> ReadNamespacedPod(const std::string &rNamespace,
                                                                      const std::string &name);

    virtual litebus::Future<std::shared_ptr<V1NodeList>> ListNode(const litebus::Option<bool> &watch = litebus::None());

    virtual litebus::Future<std::shared_ptr<V1Node>> PatchNode(const std::string &name,
                                                               const std::shared_ptr<Object> &body);

    virtual litebus::Future<std::shared_ptr<V1Lease>> CreateNamespacedLease(const std::string &rNamespace,
                                                                            const std::shared_ptr<V1Lease> &body);
    virtual litebus::Future<std::shared_ptr<V1Lease>> ReadNamespacedLease(const std::string &name,
                                                                          const std::string &rNamespace);
    virtual litebus::Future<std::shared_ptr<V1Lease>> ReplaceNamespacedLease(const std::string &name,
                                                                             const std::string &rNamespace,
                                                                             const std::shared_ptr<V1Lease> &body);

    bool RegisterWatchHandler(const std::string &modelName, const K8sEventType &eventType, const WatchHandler &handler);

    litebus::Future<std::shared_ptr<V1PodList>> DoListNamespacePod(
        const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
        const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch = litebus::None());

    litebus::Future<std::shared_ptr<V1NodeList>> DoListNode(const litebus::Option<std::string> &rContinue,
                                                            const litebus::Option<uint32_t> &limit,
                                                            const litebus::Option<bool> &watch = litebus::None());

    litebus::Future<std::shared_ptr<V1DeploymentList>> DoListNamespaceDeployment(
        const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
        const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch = litebus::None());

    std::shared_ptr<V1OwnerReference> GetOwnerReference()
    {
        return ownerReference_;
    }

    void SetK8sClientApi(const std::shared_ptr<K8sClientApi> &k8sClientApi)
    {
        ASSERT_IF_NULL(k8sClientApi);
        this->k8sClientApi_ = std::move(k8sClientApi);
    }

    void SetCoreClientApi(const std::shared_ptr<CoreClientApi> &coreClientApi)
    {
        ASSERT_IF_NULL(coreClientApi);
        this->coreClientApi_ = std::move(coreClientApi);
    }

    void SetCoordinationApi(const std::shared_ptr<CoordinationV1Api> &coordinationV1Api)
    {
        ASSERT_IF_NULL(coordinationV1Api);
        this->coordinationV1Api_ = std::move(coordinationV1Api);
    }

    void SetAutoscalingV2Api(const std::shared_ptr<AutoscalingV2Api> &api)
    {
        ASSERT_IF_NULL(api);
        this->autoscalingV2Api_ = std::move(api);
    }

    void SetK8sClient(const std::shared_ptr<ApiClient> &k8sClient)
    {
        ASSERT_IF_NULL(k8sClient);
        this->k8sClient_ = std::move(k8sClient);
    }

    void SetHealthMonitor(const std::shared_ptr<HealthMonitor> &healthMonitor)
    {
        ASSERT_IF_NULL(healthMonitor);
        this->healthMonitor_ = std::move(healthMonitor);
    }

    void SetIsK8sHealthy(bool isHealthy)
    {
        isK8sHealthy.store(isHealthy);
    }

    // for test
    [[maybe_unused]] void SetPageLimit(uint32_t limit)
    {
        pageLimit_ = limit;
    }

    void SetK8sClientRetryTime(uint32_t retryTime)
    {
        this->retryTime_ = retryTime;
    }

    void SetK8sClientCycMs(uint32_t retryCycMs)
    {
        this->retryCycMs_ = retryCycMs;
    }

    // for test
    [[maybe_unused]] uint32_t GetK8sClientRetryTime() const
    {
        return this->retryTime_;
    }

    [[maybe_unused]] void SetRetryCycMsUpper(uint32_t retryCycMsUpper)
    {
        this->retryCycMsUpper_ = retryCycMsUpper;
    }

    // for test
    [[maybe_unused]] std::shared_ptr<HealthMonitor> GetHealthMonitor()
    {
        return this->healthMonitor_;
    }

    // for test
    [[maybe_unused]] std::unordered_map<K8sStatusChangeEvent,
                                        std::unordered_map<std::string, std::function<void()>>>
    GetK8sStatusChangeHandlerMap()
    {
        return this->k8sStatusChangeHandlerMap;
    }

protected:
    void ListNamespacedPodByPage(const std::shared_ptr<V1PodList> &podList,
                                 const std::shared_ptr<litebus::Promise<std::shared_ptr<V1PodList>>> &promise,
                                 const std::string& rNamespace, const litebus::Option<std::string> &rContinue,
                                 const litebus::Option<uint32_t> &limit);

    void ListNodeByPage(const std::shared_ptr<V1NodeList> &nodeList,
                        const std::shared_ptr<litebus::Promise<std::shared_ptr<V1NodeList>>> &promise,
                        const litebus::Option<std::string> &rContinue, const litebus::Option<uint32_t> &limit);

    void ListDeploymentByPage(const std::shared_ptr<V1DeploymentList> &deployList,
                              const std::shared_ptr<litebus::Promise<std::shared_ptr<V1DeploymentList>>> &promise,
                              const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
                              const litebus::Option<uint32_t> &limit);

    template <typename T, typename Func = std::function<litebus::Future<T>()>>
    litebus::Future<T> ClientResponseHandler(const std::shared_ptr<litebus::Promise<T>> &promise,
                                             const uint32_t curRetryTime, const Func &callback,
                                             const bool &retry = true, const bool &monitor = true);

    template <typename T, typename Func = std::function<litebus::Future<T>()>>
    litebus::Future<T> CreateWithOwnerReference(const std::string rNamespace, const T &body,
                                                const std::shared_ptr<litebus::Promise<T>> &promise,
                                                const Func &callback);

    static bool IsErrorResponse(int code, const bool &retry);

    void OnK8sHealthStatusChangeHandler(bool healthy);

    void ExecuteHandler(const K8sStatusChangeEvent &eventType);

    std::shared_ptr<K8sClientApi> k8sClientApi_;
    std::shared_ptr<CoreClientApi> coreClientApi_;
    std::shared_ptr<CoordinationV1Api> coordinationV1Api_;
    std::shared_ptr<ApiClient> k8sClient_;
    std::shared_ptr<HealthMonitor> healthMonitor_;
    std::shared_ptr<AutoscalingV2Api> autoscalingV2Api_;
    std::shared_ptr<V1OwnerReference> ownerReference_{ nullptr };

private:
    uint32_t pageLimit_{DEFAULT_LIMIT};
    std::atomic<bool> isK8sHealthy{true};
    uint32_t retryTime_{5};
    uint32_t retryCycMs_{3000};
    uint32_t retryCycMsUpper_{1000};
    std::unordered_map<K8sStatusChangeEvent, std::unordered_map<std::string, std::function<void()>>>
        k8sStatusChangeHandlerMap;
};
}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_KUBE_CLIENT_H
