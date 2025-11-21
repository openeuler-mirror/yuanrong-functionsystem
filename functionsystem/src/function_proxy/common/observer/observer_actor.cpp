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

#include "observer_actor.h"

#include <async/defer.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/signal.h"
#include "logs/logging.h"
#include "metadata/metadata.h"
#include "metrics/metrics_adapter.h"
#include "common/service_json/service_json.h"
#include "common/types/instance_state.h"
#include "common/utils/generate_message.h"
#include "meta_store_kv_operation.h"
#include "common/utils/struct_transfer.h"
#include "common/utils/tenant.h"

namespace functionsystem::function_proxy {
const int32_t WATCH_TIMEOUT_MS = 30000;
const int32_t QUERY_ETCD_INTERVAL = 60000;
using messages::RuleType;

Status ObserverActor::Register()
{
    if (metaStorageAccessor_ == nullptr) {
        YRLOG_ERROR("meta store accessor is null");
        return Status(FAILED);
    }

    std::function<litebus::Future<std::shared_ptr<Watcher>>(const litebus::Future<std::shared_ptr<Watcher>> &)> after =
        [](const litebus::Future<std::shared_ptr<Watcher>> &watcher) -> litebus::Future<std::shared_ptr<Watcher>> {
        KillProcess("timeout to watch key, kill oneself.");
        return watcher;
    };
    // keep retrying to watch in 30 seconds. kill process if timeout to watch.
    auto watchOpt = WatchOption{ .prefix = true, .prevKv = false, .revision = 0, .keepRetry = true };
    YRLOG_INFO("Register watch with prefix: {}", FUNC_META_PATH_PREFIX);
    auto functionMetaSyncer = [aid(GetAID())]() -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &ObserverActor::FunctionMetaSyncer);
    };
    auto instanceInfoSyncer = [aid(GetAID())]() -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &ObserverActor::InstanceInfoSyncer);
    };
    auto busProxySyncer = [aid(GetAID())]() -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &ObserverActor::BusProxySyncer);
    };
    (void)metaStorageAccessor_
        ->RegisterObserver(FUNC_META_PATH_PREFIX, watchOpt,
                           [aid(GetAID())](const std::vector<WatchEvent> &events, bool) {
                               auto respCopy = events;
                               litebus::Async(aid, &ObserverActor::UpdateFuncMetaEvent, respCopy);
                               return true;
                           }, functionMetaSyncer)
        .After(WATCH_TIMEOUT_MS, after);

    YRLOG_INFO("Register watch with prefix: {}", BUSPROXY_PATH_PREFIX);
    (void)metaStorageAccessor_
        ->RegisterObserver(BUSPROXY_PATH_PREFIX, watchOpt,
                           [aid(GetAID())](const std::vector<WatchEvent> &events, bool) {
                               auto respCopy = events;
                               litebus::Async(aid, &ObserverActor::UpdateProxyEvent, respCopy);
                               return true;
                           }, busProxySyncer)
        .After(WATCH_TIMEOUT_MS, after);

    auto synced = metaStorageAccessor_->Sync(INSTANCE_PATH_PREFIX, true);
    UpdateInstanceEvent(synced.first, true);
    YRLOG_DEBUG("sync key({}) finished", INSTANCE_PATH_PREFIX);
    instanceSyncDone_.SetValue(true);

    if (!isPartialWatchInstances_) {
        YRLOG_INFO("Register watch with prefix: {}", INSTANCE_ROUTE_PATH_PREFIX);
        watchOpt = WatchOption{ true, false, synced.second + 1, true };
        (void)metaStorageAccessor_
            ->RegisterObserver(
                INSTANCE_ROUTE_PATH_PREFIX, watchOpt,
                [aid(GetAID())](const std::vector<WatchEvent> &events, bool synced) {
                    auto respCopy = events;
                    litebus::Async(aid, &ObserverActor::UpdateInstanceRouteEvent, respCopy, synced);
                    return true;
                },
                instanceInfoSyncer)
            .After(WATCH_TIMEOUT_MS, after);
    }

    YRLOG_INFO("load local function");
    LoadLocalFuncMeta(funcMetaMap_, observerParam_.functionMetaPath);
    service_json::LoadFuncMetaFromServiceYaml(funcMetaMap_, observerParam_.servicesPath, observerParam_.libPath);
    for (auto it = funcMetaMap_.begin(); it != funcMetaMap_.end(); ++it) {
        localFuncMetaSet_.emplace(it->first);
    }
    if (updateFuncMetasFunc_ != nullptr) {
        updateFuncMetasFunc_(true, funcMetaMap_);
    }
    return Status::OK();
}

void ObserverActor::OnTenantInstanceEvent(const std::string &instanceID,
                                          const resource_view::InstanceInfo &instanceInfo)
{
    TenantEvent tenantEvent = {
        .tenantID = instanceInfo.tenantid(),
        .functionProxyID = instanceInfo.functionproxyid(),
        .functionAgentID = instanceInfo.functionagentid(),
        .instanceID = instanceID,
        .agentPodIp = GetAgentPodIpFromRuntimeAddress(instanceInfo.runtimeaddress()),
        .code = instanceInfo.instancestatus().code(),
    };
    YRLOG_DEBUG(
        "receive tenant instance event, tenantID({}), functionProxyID({}), functionAgentID({}),"
        " instanceID({}) agentPodIp({}), code({})",
        tenantEvent.tenantID, tenantEvent.functionProxyID, tenantEvent.functionAgentID, instanceID,
        tenantEvent.agentPodIp, tenantEvent.code);

    lastTenantEventCacheMap_[instanceID] = tenantEvent;
    NotifyUpdateTenantInstance(tenantEvent);
}

void ObserverActor::UpdateInstanceEvent(const std::vector<WatchEvent> &events, bool synced)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto keyInfo = ParseInstanceKey(eventKey);
        auto instanceID = keyInfo.instanceID;
        YRLOG_DEBUG("receive instance event, instance({}), type: {}, key: {}, revision: {}", instanceID,
                    event.eventType, eventKey, event.kv.mod_revision());
        auto iter = instanceModRevisionMap_.find(instanceID);
        if (iter == instanceModRevisionMap_.end() && event.eventType == EVENT_TYPE_DELETE) {
            YRLOG_WARN("receive non-existed instance({}) delete event, ignore, revision({})", instanceID,
                       event.kv.mod_revision());
            continue;
        }

        if (iter != instanceModRevisionMap_.end() && iter->second > event.kv.mod_revision()) {
            YRLOG_ERROR("receive old instance({}) event ignore, coming revision({}), current revision({})", instanceID,
                        event.kv.mod_revision(), iter->second);
            continue;
        }

        HandleInstanceEvent(synced, event, instanceID);
    }
}

void ObserverActor::HandleInstanceEvent(bool synced, const WatchEvent &event, std::string &instanceID)
{
    auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
    switch (event.eventType) {
        case EVENT_TYPE_PUT: {
            InstanceInfo instanceInfo;
            if (!TransToInstanceInfoFromJson(instanceInfo, event.kv.value())) {
                YRLOG_ERROR("failed to trans to instanceInfo from json string, instance({})", instanceID);
                break;
            }
            // Forward Compatible
            if (instanceInfo.unitid().empty()) {
                instanceInfo.set_unitid(instanceInfo.functionagentid());
            }
            // sync during restart
            if (isPartialWatchInstances_) {
                if (instanceInfo.parentfunctionproxyaid().find(nodeID_) != std::string::npos
                    || instanceInfo.functionproxyid() == nodeID_) {
                    WatchInstance(instanceID);
                } else {
                    YRLOG_DEBUG("instance({}) parent({}), on {} doesn't belong to this node({}), skip event",
                                instanceID, instanceInfo.parentfunctionproxyaid(), instanceInfo.functionproxyid(),
                                nodeID_);
                    break;
                }
            }

            SetInstanceBillingContext(instanceInfo, synced);
            YRLOG_DEBUG("receive instance put event, instance({}), runtime({}), proxy({}), status({}), reason({})",
                        instanceID, instanceInfo.runtimeid(), instanceInfo.functionproxyid(),
                        instanceInfo.instancestatus().code(), instanceInfo.instancestatus().msg());
            (*instanceInfo.mutable_extensions())[INSTANCE_MOD_REVISION] = std::to_string(event.kv.mod_revision());
            PutInstanceEvent(instanceInfo, synced, event.kv.mod_revision());

            litebus::Async(GetAID(), &ObserverActor::ReportInstanceStatus, instanceID,
                           instanceInfo.instancestatus().code(), instanceInfo.function());
            break;
        }
        case EVENT_TYPE_DELETE: {
            YRLOG_DEBUG("receive instance delete event, instance({}), key({})", instanceID, eventKey);
            DelInstanceEvent(instanceID);
            break;
        }
        default: {
            YRLOG_WARN("unknown event type {}", event.eventType);
        }
    }
}

void ObserverActor::UpdateInstanceRouteEvent(const std::vector<WatchEvent> &events, bool synced)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto keyInfo = ParseInstanceKey(eventKey);
        auto instanceID = keyInfo.instanceID;
        YRLOG_DEBUG("receive routeInfo event, instance({}), type: {}, key: {}, revision: {}", instanceID,
                    event.eventType, eventKey, event.kv.mod_revision());

        auto iter = instanceModRevisionMap_.find(instanceID);
        if (iter == instanceModRevisionMap_.end() && event.eventType == EVENT_TYPE_DELETE) {
            YRLOG_WARN("receive non-existed instance({}) delete event, ignore, revision({})", instanceID,
                       event.kv.mod_revision());
            continue;
        }

        if (iter != instanceModRevisionMap_.end() && iter->second >= event.kv.mod_revision()) {
            YRLOG_ERROR("receive old instance({}) event ignore, coming revision({}), current revision({})", instanceID,
                        event.kv.mod_revision(), iter->second);
            continue;
        }
        HandleRouteEvent(synced, event, instanceID);
    }
}

void ObserverActor::HandleRouteEvent(bool synced, const WatchEvent &event, std::string &instanceID)
{
    switch (event.eventType) {
        case EVENT_TYPE_PUT: {
            RouteInfo routeInfo;
            if (!TransToRouteInfoFromJson(routeInfo, event.kv.value())) {
                YRLOG_ERROR("failed to trans to routeInfo from json string, instance({})", instanceID);
                break;
            }
            YRLOG_DEBUG("receive routeInfo put event, instance({}), proxy({}), status({}), reason({})", instanceID,
                        routeInfo.functionproxyid(), routeInfo.instancestatus().code(),
                        routeInfo.instancestatus().msg());

            InstanceInfo instanceInfo;
            if (instanceInfoMap_.find(instanceID) != instanceInfoMap_.end()) {
                YRLOG_DEBUG("find and update instance({})", instanceID);
                instanceInfo = instanceInfoMap_[instanceID];
            }
            TransToInstanceInfoFromRouteInfo(routeInfo, instanceInfo);
            (*instanceInfo.mutable_extensions())[INSTANCE_MOD_REVISION] = std::to_string(event.kv.mod_revision());
            PutInstanceEvent(instanceInfo, synced, event.kv.mod_revision());

            litebus::Async(GetAID(), &ObserverActor::ReportInstanceStatus, instanceID,
                           instanceInfo.instancestatus().code(), instanceInfo.function());
            break;
        }
        case EVENT_TYPE_DELETE: {
            YRLOG_DEBUG("receive routeInfo delete event, instance({})", instanceID);
            DelInstanceEvent(instanceID);
            break;
        }
        default: {
            YRLOG_WARN("unknown event type {}", event.eventType);
        }
    }
}

void ObserverActor::ReportInstanceStatus(const std::string &instanceID, const int status,
                                         const std::string &functionKey)
{
    auto timeStamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    struct functionsystem::metrics::MeterTitle instanceTitle {
        "yr_app_instance_status",
            "instance status code: 1-Scheduling, 2-Creating, 3-Running, 4-Failed, 5-Exited, 6-Fatal, 7-ScheduleFailed",
            "enum"
    };
    struct functionsystem::metrics::MeterData data {
        static_cast<double>(status),
            {
                { "instance_id", instanceID },
                { "function_key", functionKey },
                { "timestamp", std::to_string(timeStamp) },
            },
    };
    functionsystem::metrics::MetricsAdapter::GetInstance().ReportGauge(instanceTitle, data);
}

void ObserverActor::SetInstanceInfo(const std::string &instanceID, const resource_view::InstanceInfo &info)
{
    // if instance is function_accessor or driver, and located in this node, need to call a callback function
    // and notify instance control to build a grpc stream to FunctionAccessor or driver.
    if (IsDriver(info)) {
        YRLOG_INFO("receive driver put event, instance({})", instanceID);
        if (nodeID_ == info.functionproxyid() && driverEventCbFunc_ != nullptr) {
            driverEventCbFunc_(info);
        }
    }

    instanceInfoMap_[instanceID] = info;

    const auto &funcAgentID = info.functionagentid();
    if (!funcAgentID.empty()) {
        agentInstanceInfoMap_[funcAgentID][instanceID] = info;
    }

    if (auto iter = localInstanceInfo_.find(instanceID);
        iter != localInstanceInfo_.end() && iter->second.functionproxyid() != nodeID_) {
        // instance is migrated to another node
        (void)localInstanceInfo_.erase(instanceID);
    }

    if (info.functionproxyid() == nodeID_) {
        localInstanceInfo_[instanceID] = info;
    }
}

void ObserverActor::DelInstanceInfo(const std::string &instanceID)
{
    Status s(ERR_INSTANCE_EXITED);
    s.AppendMessage("instance({" + instanceID + "}) already exited");
    if (instanceInfoMap_.find(instanceID) == instanceInfoMap_.end()) {
        YRLOG_WARN("instance({}) not in map", instanceID);
        return;
    }
    auto instanceInfo = instanceInfoMap_[instanceID];
    s.AppendMessage(instanceInfo.instancestatus().msg());
    if (agentInstanceInfoMap_.find(instanceInfo.functionagentid()) != agentInstanceInfoMap_.end()) {
        (void)agentInstanceInfoMap_[instanceInfo.functionagentid()].erase(instanceID);
        if (agentInstanceInfoMap_[instanceInfo.functionagentid()].empty()) {
            (void)agentInstanceInfoMap_.erase(instanceInfo.functionagentid());
        }
    }

    (void)instanceModRevisionMap_.erase(instanceID);
    (void)localInstanceInfo_.erase(instanceID);
    (void)instanceInfoMap_.erase(instanceID);
}

void ObserverActor::CloseDataInterfaceClient(const std::string &instanceID)
{
    ASSERT_IF_NULL(dataInterfaceClientManager_);
    (void)dataInterfaceClientManager_->DeleteClient(instanceID);
}

litebus::Future<Status> ObserverActor::DelInstanceEvent(const std::string &instanceID)
{
    NotifyDeleteInstance(instanceID);
    DelInstanceInfo(instanceID);
    CloseDataInterfaceClient(instanceID);
    if (observerParam_.enableTenantAffinity) {
        NotifyDeleteTenantInstance(lastTenantEventCacheMap_[instanceID]);
        lastTenantEventCacheMap_.erase(instanceID);
    }
    if (isPartialWatchInstances_) {
        // delete watch when receive delete event
        CancelWatchInstance(instanceID);
    }
    return Status::OK();
}

void ObserverActor::UpdateFuncMetaEvent(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto funcKey = GetFuncKeyFromFuncMetaPath(eventKey);
        if (funcKey.empty()) {
            YRLOG_WARN("function key is empty, path: {}", eventKey);
            continue;
        }
        YRLOG_DEBUG("receive function meta event, type: {}, funKey: {}, path: {}", event.eventType, funcKey, eventKey);

        ProcFuncMetaEvent(funcKey, event);
        RemoveQueryKeyMetaCache(eventKey);
    }
}

void ObserverActor::ProcFuncMetaEvent(const std::string &funcKey, const functionsystem::WatchEvent &event)
{
    switch (event.eventType) {
        case EVENT_TYPE_PUT: {
            // Need to delete the function before upgrade different type function.
            auto funcMeta = GetFuncMetaFromJson(event.kv.value());
            OnPutMeta(false, funcKey, funcMeta);
            break;
        }
        case EVENT_TYPE_DELETE: {
            funcMetaMap_.erase(funcKey);
            if (updateFuncMetasFunc_ != nullptr) {
                updateFuncMetasFunc_(false, { { funcKey, {} } });
            }
            break;
        }
        default: {
            YRLOG_WARN("unknown event type {}", event.eventType);
            break;
        }
    }
}

void ObserverActor::OnPutMeta(const litebus::Future<bool> &isSystem, const std::string &funcKey,
                              const FunctionMeta &funcMeta)
{
    auto meta = funcMeta;  // copy
    if (isSystem.IsError()) {
        YRLOG_ERROR("failed to check system");
        return;
    }

    if (isSystem.Get()) {
        if (funcMetaMap_.find(funcKey) != funcMetaMap_.end()) {
            YRLOG_ERROR("The system function({}) type conflicts and cannot be updated.", funcKey);
            return;
        }
        YRLOG_DEBUG("update system function meta-info, funcKey({})", funcKey);
        meta.funcMetaData.isSystemFunc = true;
        systemFuncMetaMap_[funcKey] = meta;
        if (updateFuncMetasFunc_ != nullptr) {
            updateFuncMetasFunc_(true, { { funcKey, meta } });
        }
    } else {
        if (systemFuncMetaMap_.find(funcKey) != systemFuncMetaMap_.end()) {
            YRLOG_ERROR("The function({}) type conflicts and cannot be updated.", funcKey);
            return;
        }
        meta.funcMetaData.isSystemFunc = false;
        funcMetaMap_[funcKey] = meta;
        if (updateFuncMetasFunc_ != nullptr) {
            updateFuncMetasFunc_(true, { { funcKey, meta } });
        }
    }
}

litebus::Future<Status> ObserverActor::PutInstance(const resource_view::InstanceInfo &instanceInfo, bool isForceUpdate)
{
    if (instanceOperator_ == nullptr) {
        YRLOG_ERROR("meta store accessor is null");
        return Status(StatusCode::LS_META_STORE_ACCESSOR_IS_NULL);
    }

    auto path = GenInstanceKey(instanceInfo.function(), instanceInfo.instanceid(), instanceInfo.requestid());
    if (path.IsNone()) {
        YRLOG_ERROR("failed to get instance key from InstanceInfo");
        return Status(StatusCode::FAILED);
    }

    std::string jsonStr;
    if (!TransToJsonFromInstanceInfo(jsonStr, instanceInfo)) {
        YRLOG_ERROR("failed to trans to json string from InstanceInfo");
        return Status(StatusCode::FAILED);
    }
    YRLOG_DEBUG("put instance to meta store, instance({}), function: {}, path: {}, instance status: {}",
                instanceInfo.instanceid(), instanceInfo.function(), path.Get(), instanceInfo.instancestatus().code());

    std::shared_ptr<StoreInfo> instancePutInfo = std::make_shared<StoreInfo>(path.Get(), jsonStr);
    std::shared_ptr<StoreInfo> routePutInfo;

    auto state = static_cast<InstanceState>(instanceInfo.instancestatus().code());
    if (functionsystem::NeedUpdateRouteState(state, isMetaStoreEnabled_)) {
        auto routePath = GenInstanceRouteKey(instanceInfo.instanceid());
        resource_view::RouteInfo routeInfo;
        TransToRouteInfoFromInstanceInfo(instanceInfo, routeInfo);
        std::string routeJsonStr;
        if (!TransToJsonFromRouteInfo(routeJsonStr, routeInfo)) {
            YRLOG_ERROR("failed to transfer RouteInfo to json for key: {}", routePath);
            return Status(StatusCode::FAILED);
        }

        routePutInfo = std::make_shared<StoreInfo>(routePath, routeJsonStr);
    }
    if (isPartialWatchInstances_) {
        WatchInstance(instanceInfo.instanceid());
    }
    return instanceOperator_->Create(instancePutInfo, routePutInfo, IsLowReliabilityInstance(instanceInfo))
        .Then([aid(GetAID()), instancePutInfo, routePutInfo, instanceInfo, isForceUpdate](const OperateResult &result) {
            if (result.status.IsOk()) {
                // fast publish
                litebus::Async(aid, &ObserverActor::PutInstanceEvent, instanceInfo, isForceUpdate,
                               result.currentModRevision);
                return Status::OK();
            }
            YRLOG_ERROR("failed to put key {} using meta client, error: {}", instancePutInfo->key,
                        result.status.GetMessage());
            if (routePutInfo != nullptr) {
                YRLOG_ERROR("failed to put key {} using meta client, error: {}", routePutInfo->key,
                            result.status.GetMessage());
            }
            return Status(StatusCode::BP_META_STORAGE_PUT_ERROR,
                          "failed to create key, err: " + result.status.GetMessage());
        });
}

void ObserverActor::PutInstanceEvent(const resource_view::InstanceInfo &instanceInfo, bool isForceUpdate,
                                     int64_t modRevision)
{
    if (modRevision != 0 || instanceModRevisionMap_.find(instanceInfo.instanceid()) == instanceModRevisionMap_.end()) {
        // update mod_revision
        instanceModRevisionMap_[instanceInfo.instanceid()] = modRevision;
    }
    SetInstanceInfo(instanceInfo.instanceid(), instanceInfo);
    NotifyUpdateInstance(instanceInfo.instanceid(), instanceInfo, isForceUpdate);

    if (observerParam_.enableTenantAffinity) {
        OnTenantInstanceEvent(instanceInfo.instanceid(), instanceInfo);
    }
}

void ObserverActor::FastPutRemoteInstanceEvent(const resource_view::InstanceInfo &instanceInfo, bool synced,
                                               int64_t modRevision)
{
    auto instanceID = instanceInfo.instanceid();
    auto iter = instanceModRevisionMap_.find(instanceInfo.instanceid());
    if (modRevision != 0 && iter != instanceModRevisionMap_.end() && modRevision <= iter->second) {
        // instance is in cache, but revision is old
        YRLOG_WARN("ignore remote instance({}) event, mod revision({}) current({})", instanceInfo.instanceid(),
                   modRevision, iter->second);
        WatchInstance(instanceID, modRevision);
        return;
    }
    if (modRevision != 0 && iter == instanceModRevisionMap_.end()) {
        // if instance not found in cache, fetch it from etcd to check instance exists, to avoid callResult is delay to
        // receive
        GetInstanceRouteInfo(instanceInfo.instanceid())
            .OnComplete(
                [instanceID, aid(GetAID()), modRevision](const litebus::Future<resource_view::InstanceInfo> &future) {
                    if (future.IsError()) {
                        YRLOG_ERROR("failed to GetInstanceRouteInfo for {}, don't need to watch instance", instanceID);
                        return;
                    }
                    litebus::Async(aid, &ObserverActor::WatchInstance, instanceID, modRevision);
                });
        return;
    }
    PutInstanceEvent(instanceInfo, synced, modRevision);
    WatchInstance(instanceID, modRevision);
}

litebus::Future<Status> ObserverActor::DelInstance(const std::string &instanceID)
{
    if (metaStorageAccessor_ == nullptr) {
        YRLOG_ERROR("meta store accessor is null");
        return Status(StatusCode::LS_META_STORE_ACCESSOR_IS_NULL);
    }

    if (instanceInfoMap_.find(instanceID) == instanceInfoMap_.end()) {
        YRLOG_WARN("there is no element of instance({})", instanceID);
        return Status(StatusCode::SUCCESS);
    }

    auto instanceInfo = instanceInfoMap_[instanceID];
    auto path = GenInstanceKey(instanceInfo.function(), instanceInfo.instanceid(), instanceInfo.requestid());
    if (path.IsNone()) {
        YRLOG_ERROR("failed to get instance key from InstanceInfo");
        return Status(StatusCode::FAILED);
    }
    DelInstanceEvent(instanceID);
    YRLOG_DEBUG("delete instance to meta store, instance({}), instance status: {}, functionKey: {}, path: {}",
                instanceInfo.instanceid(), instanceInfo.instancestatus().code(), instanceInfo.function(), path.Get());

    std::shared_ptr<StoreInfo> routePutInfo;
    auto state = static_cast<InstanceState>(instanceInfo.instancestatus().code());
    if (functionsystem::NeedUpdateRouteState(state, isMetaStoreEnabled_)) {
        auto routePath = GenInstanceRouteKey(instanceInfo.instanceid());
        routePutInfo = std::make_shared<StoreInfo>(routePath, "");
    }

    return metaStorageAccessor_->Delete(path.Get())
        .Then([accessor(metaStorageAccessor_), routePutInfo,
               requestID(instanceInfo.requestid())](const Status &status) -> litebus::Future<Status> {
            if (status.IsOk()) {
                if (routePutInfo != nullptr) {
                    YRLOG_DEBUG("{}|try to delete routeInfo", requestID);
                    return accessor->Delete(routePutInfo->key);
                }
                return Status::OK();
            }
            return status;
        });
}

litebus::Future<litebus::Option<FunctionMeta>> ObserverActor::GetFuncMeta(const std::string &funcKey)
{
    auto funcMeta = functionsystem::GetFuncMeta(funcKey, funcMetaMap_, systemFuncMetaMap_);
    if (funcMeta.IsSome()) {
        return funcMeta;
    }
    if (queryFuncMetaPromiseMap_.find(funcKey) != queryFuncMetaPromiseMap_.end()) {
        return queryFuncMetaPromiseMap_[funcKey]->GetFuture();
    }
    auto etcdKey = GenEtcdFullFuncKey(funcKey);
    if (etcdKey.empty()) {
        return funcMeta;
    }
    if (queryMetaStoreTimerMap_.find(etcdKey) != queryMetaStoreTimerMap_.end()) {
        YRLOG_DEBUG("skip query from meta-store for {}", funcKey);
        return funcMeta;
    }

    auto promise = std::make_shared<litebus::Promise<litebus::Option<FunctionMeta>>>();
    queryFuncMetaPromiseMap_[funcKey] = promise;
    // need remove cache
    queryMetaStoreTimerMap_[etcdKey] =
        litebus::AsyncAfter(QUERY_ETCD_INTERVAL, GetAID(), &ObserverActor::RemoveQueryKeyMetaCache, etcdKey);
    //  Get from meteStore will always have response, use then is also ok
    GetFuncMetaFromMetaStore(GenEtcdFullFuncKey(funcKey))
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnGetFuncMetaFromMetaStore, funcKey, std::placeholders::_1));
    return promise->GetFuture();
}

litebus::Future<litebus::Option<resource_view::InstanceInfo>> ObserverActor::GetInstanceInfoByID(
    const std::string &instanceID)
{
    if (instanceInfoMap_.find(instanceID) != instanceInfoMap_.end()) {
        return instanceInfoMap_[instanceID];
    }
    YRLOG_WARN("{} does not exists in instanceInfoMap", instanceID);
    return metaStorageAccessor_->GetMetaClient()
        ->Get(GenInstanceRouteKey(instanceID), {})
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnGetInstanceFromMetaStore, std::placeholders::_1, instanceID))
        .Then([](litebus::Future<resource_view::InstanceInfo> res)
                  -> litebus::Future<litebus::Option<resource_view::InstanceInfo>> {
            if (res.IsError() || res.Get().instanceid().empty()) {
                return litebus::None();
            }
            return res.Get();
        });
}

litebus::Option<InstanceInfoMap> ObserverActor::GetAgentInstanceInfoByID(const std::string &funcAgentID)
{
    if (agentInstanceInfoMap_.find(funcAgentID) == agentInstanceInfoMap_.end()) {
        YRLOG_WARN("there is no element of funcAgentID: {}", funcAgentID);
        return litebus::None();
    }

    return agentInstanceInfoMap_[funcAgentID];
}

litebus::Option<InstanceInfoMap> ObserverActor::GetLocalInstanceInfo()
{
    if (localInstanceInfo_.empty()) {
        return litebus::None();
    }
    return localInstanceInfo_;
}

void ObserverActor::UpdateProxyEvent(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        YRLOG_DEBUG("receive proxy event {}  {}", eventKey, event.kv.value());
        auto node = GetProxyNode(eventKey);
        if (node == nodeID_ && event.eventType == EVENT_TYPE_DELETE) {
            YRLOG_WARN("receive self proxy delete event {}", eventKey);
            litebus::AID aid;
            aid.SetName("function_proxy" + nodeID_);
            aid.SetUrl(GetAID().UnfixUrl());
            aid.SetAk(GetAID().GetAK());
            auto info = GetServiceRegistryInfo(nodeID_, aid);
            auto ttl = TtlValidate(observerParam_.serviceTTL) ? observerParam_.serviceTTL : DEFAULT_TTL;
            metaStorageAccessor_->PutWithLease(info.key, function_proxy::Dump(info.meta), ttl);
        }

        // ignore self event
        if (node == nodeID_) {
            YRLOG_WARN("ignore received proxy event {}  {}", eventKey, event.kv.value());
            continue;
        }
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                auto proxyMeta = GetProxyMeta(event.kv.value());
                PutProxyMeta(proxyMeta);
                break;
            }
            case EVENT_TYPE_DELETE: {
                proxyView_->Delete(node);
                localSchedulerView_->Delete(node);
                break;
            }
            default: {
                YRLOG_WARN("unknown event type {}", event.eventType);
            }
        }
    }
}

void ObserverActor::PutProxyMeta(const ProxyMeta &proxyMeta)
{
    auto dst = litebus::AID(proxyMeta.aid);
    dst.SetAk(proxyMeta.ak);
    auto client = std::make_shared<proxy::Client>(dst);
    proxyView_->Update(proxyMeta.node, client);
    auto localAID =
        std::make_shared<litebus::AID>(proxyMeta.node + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX, dst.Url());
    localAID->SetAk(proxyMeta.ak);
    localSchedulerView_->Update(proxyMeta.node, localAID);
}

litebus::Future<litebus::Option<litebus::AID>> ObserverActor::GetLocalSchedulerAID(const std::string &proxyID)
{
    auto localSchedulerAID = localSchedulerView_->Get(proxyID);
    if (localSchedulerAID != nullptr) {
        YRLOG_DEBUG("query local scheduler aid of proxyID({}) is {}", proxyID, localSchedulerAID->HashString());
        return *localSchedulerAID;
    }
    if (queryProxyPromiseMap_.find(proxyID) != queryProxyPromiseMap_.end()) {
        return queryProxyPromiseMap_[proxyID]->GetFuture();
    }
    auto key = BUSPROXY_PATH_PREFIX + "/0/node/" + proxyID;
    if (queryMetaStoreTimerMap_.find(key) != queryMetaStoreTimerMap_.end()) {
        return litebus::None();
    }
    auto promise = std::make_shared<litebus::Promise<litebus::Option<litebus::AID>>>();
    queryProxyPromiseMap_[proxyID] = promise;
    queryMetaStoreTimerMap_[key] =
        litebus::AsyncAfter(QUERY_ETCD_INTERVAL, GetAID(), &ObserverActor::RemoveQueryKeyMetaCache, key);
    GetProxyFromMetaStore(key).Then(
        litebus::Defer(GetAID(), &ObserverActor::OnGetProxyFromMetaStore, proxyID, std::placeholders::_1));
    return promise->GetFuture();
}

bool IsSchedulingInstanceOfGroup(const resource_view::InstanceInfo &info)
{
    return !info.groupid().empty() && info.instancestatus().code() == static_cast<int32_t>(InstanceState::SCHEDULING);
}

std::vector<std::string> ObserverActor::GetLocalInstances()
{
    std::vector<std::string> localInstances;
    for (const auto &[instanceID, info] : instanceInfoMap_) {
        if (info.functionproxyid() == nodeID_) {
            // while instance of group is scheduling, the meta info would not persist to backend which means
            // instance-manager would not able to take over it.
            if (IsDriver(info) || IsSchedulingInstanceOfGroup(info)) {  // static ?
                continue;
            }
            (void)localInstances.emplace_back(instanceID);
        }
    }
    return localInstances;
}

void ObserverActor::Attach(const std::shared_ptr<InstanceListener> &listener)
{
    if (listener != nullptr) {
        instanceListenerList_.push_back(listener);
    }
}

void ObserverActor::Detach(const std::shared_ptr<InstanceListener> &listener)
{
    if (listener != nullptr) {
        instanceListenerList_.remove(listener);
    }
}

void ObserverActor::AttachTenantListener(const std::shared_ptr<TenantListener> &listener)
{
    if (listener != nullptr) {
        tenantListenerList_.push_back(listener);
    }
}

void ObserverActor::DetachTenantListener(const std::shared_ptr<TenantListener> &listener)
{
    if (listener != nullptr) {
        tenantListenerList_.remove(listener);
    }
}

void ObserverActor::NotifyUpdateInstance(const std::string &instanceID, const resource_view::InstanceInfo &instanceInfo,
                                         bool isForceUpdate)
{
    auto iterator = instanceListenerList_.begin();
    while (iterator != instanceListenerList_.end()) {
        (*iterator)->Update(instanceID, instanceInfo, isForceUpdate);
        ++iterator;
    }
}

void ObserverActor::NotifyDeleteInstance(const std::string &instanceID)
{
    auto iterator = instanceListenerList_.begin();
    while (iterator != instanceListenerList_.end()) {
        (*iterator)->Delete(instanceID);  // message_是从etcd监听到的事件。
        ++iterator;
    }
}

void ObserverActor::NotifyUpdateTenantInstance(const TenantEvent &event)
{
    auto iterator = tenantListenerList_.begin();
    while (iterator != tenantListenerList_.end()) {
        (*iterator)->OnTenantUpdateInstance(event);
        ++iterator;
    }
}

void ObserverActor::NotifyDeleteTenantInstance(const TenantEvent &event)
{
    auto iterator = tenantListenerList_.begin();
    while (iterator != tenantListenerList_.end()) {
        (*iterator)->OnTenantDeleteInstance(event);
        ++iterator;
    }
}

litebus::Future<bool> ObserverActor::InstanceSyncDone()
{
    return instanceSyncDone_.GetFuture();
}

litebus::Future<Status> ObserverActor::SubscribeInstanceEvent(const std::string &subscriber,
                                                              const std::string &targetInstance, bool ignoreNonExist)
{
    return instanceView_->SubscribeInstanceEvent(subscriber, targetInstance, ignoreNonExist);
}

litebus::Future<Status> ObserverActor::TrySubscribeInstanceEvent(const std::string &subscriber,
                                                                 const std::string &targetInstance, bool ignoreNonExist)
{
    // if this instance hasn't been watched on this node, get and watch first
    if (isPartialWatchInstances_ && (instanceWatchers_.find(targetInstance) == instanceWatchers_.end()
        || instanceWatchers_[targetInstance] == nullptr)) {
        auto promise = std::make_shared<litebus::Promise<Status>>();
        GetAndWatchInstance(targetInstance)
            .OnComplete([promise, aid(GetAID()), subscriber, targetInstance,
                         ignoreNonExist](const litebus::Future<resource_view::InstanceInfo> &future) {
                if (future.IsError()) {
                    // don't return
                    YRLOG_WARN("failed to get instance({}) from meta-store", targetInstance);
                }
                promise->Associate(litebus::Async(aid, &ObserverActor::SubscribeInstanceEvent, subscriber,
                                                  targetInstance, ignoreNonExist));
            });
        return promise->GetFuture();
    }
    /* In the following cases, need to query metastore to check whether the instance exists:
      1. If instance A is used as a handle, it is passed to another instance B. The proxy of instanceB may not have
      instance A in cache
      2. There are multi frontend instances, if create request is from frontend A, but invoke request is from frontend B
      */
    auto promise = std::make_shared<litebus::Promise<Status>>();
    GetInstanceRouteInfo(targetInstance)
        .OnComplete([subscriber, targetInstance, ignoreNonExist, aid(GetAID()),
                     promise](const litebus::Future<resource_view::InstanceInfo> &future) {
            if (future.IsError()) {
                // don't return
                YRLOG_WARN("failed to get instance({}) from meta-store", targetInstance);
            }
            promise->Associate(litebus::Async(aid, &ObserverActor::SubscribeInstanceEvent, subscriber, targetInstance,
                                              ignoreNonExist));
        });
    return promise->GetFuture();
}

void ObserverActor::NotifyMigratingRequest(const std::string &instanceID)
{
    instanceView_->NotifyMigratingRequest(instanceID);
}

void ObserverActor::SetInstanceBillingContext(const resource_view::InstanceInfo &instanceInfo, bool synced)
{
    if (synced && instanceInfo.functionproxyid() == nodeID_) {
        auto customMetricsOption = metrics::MetricsAdapter::GetInstance().GetMetricsContext().
            GetCustomMetricsOption(instanceInfo);
        if (instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING) ||
            instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::EXITING) ||
            instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::FAILED) ||
            instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::FATAL)) {
            metrics::MetricsAdapter::GetInstance().GetMetricsContext().InitExtraBillingInstance(
                instanceInfo.instanceid(), customMetricsOption, instanceInfo.issystemfunc());
            metrics::MetricsAdapter::GetInstance().RegisterBillingInstanceRunningDuration();
        }
        if (instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING) ||
            instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::EXITING)) {
            metrics::MetricsAdapter::GetInstance().GetMetricsContext().InitBillingInstance(
                instanceInfo.instanceid(), customMetricsOption, instanceInfo.issystemfunc());
            metrics::MetricsAdapter::GetInstance().RegisterBillingInstanceRunningDuration();
        }
    }
}

litebus::Future<litebus::Option<FunctionMeta>> ObserverActor::GetFuncMetaFromMetaStore(const std::string &funcKey)
{
    return metaStorageAccessor_->AsyncGet(funcKey).Then(
        [](const litebus::Option<std::string> resp) -> litebus::Future<litebus::Option<FunctionMeta>> {
            if (resp.IsNone()) {
                return litebus::None();
            }
            return GetFuncMetaFromJson(resp.Get());
        });
}

Status ObserverActor::OnGetFuncMetaFromMetaStore(const std::string &funcKey,
                                                 const litebus::Option<FunctionMeta> &funcMeta)
{
    if (queryFuncMetaPromiseMap_.find(funcKey) != queryFuncMetaPromiseMap_.end()) {
        queryFuncMetaPromiseMap_[funcKey]->SetValue(funcMeta);
        queryFuncMetaPromiseMap_.erase(funcKey);
    }
    if (funcMeta.IsSome()) {
        auto meta = funcMeta.Get();
        // if func is not system function(tenant=0) and cache not exist
        if (meta.funcMetaData.tenantId != "0" && funcMetaMap_.find(funcKey) == funcMetaMap_.end()) {
            funcMetaMap_[funcKey] = meta;
        }
    }
    return Status::OK();
}

litebus::Future<litebus::Option<ProxyMeta>> ObserverActor::GetProxyFromMetaStore(const std::string &key)
{
    return metaStorageAccessor_->AsyncGet(key).Then(
        [](const litebus::Option<std::string> resp) -> litebus::Future<litebus::Option<ProxyMeta>> {
            if (resp.IsNone()) {
                return litebus::None();
            }
            return GetProxyMeta(resp.Get());
        });
}

Status ObserverActor::OnGetProxyFromMetaStore(const std::string &proxyID, const litebus::Option<ProxyMeta> &proxyMeta)
{
    std::shared_ptr<litebus::Promise<litebus::Option<litebus::AID>>> promise = nullptr;
    if (queryProxyPromiseMap_.find(proxyID) != queryProxyPromiseMap_.end()) {
        promise = queryProxyPromiseMap_[proxyID];
        (void)queryProxyPromiseMap_.erase(proxyID);
    }
    if (proxyMeta.IsSome()) {
        YRLOG_WARN("succeed to get proxy {} from metastore", proxyID);
        PutProxyMeta(proxyMeta.Get());
        auto localSchedulerAID = localSchedulerView_->Get(proxyID);
        if (localSchedulerAID != nullptr && promise != nullptr) {
            promise->SetValue(litebus::Option<litebus::AID>(*localSchedulerAID));
            return Status::OK();
        }
    }
    YRLOG_WARN("failed to get proxy {} from metastore", proxyID);
    if (promise != nullptr) {
        promise->SetValue(litebus::Option<litebus::AID>());
    }
    return Status::OK();
}

void ObserverActor::RemoveQueryKeyMetaCache(const std::string &key)
{
    if (queryMetaStoreTimerMap_.find(key) == queryMetaStoreTimerMap_.end()) {
        return;
    }
    auto timer = queryMetaStoreTimerMap_[key];
    litebus::TimerTools::Cancel(timer);
    queryMetaStoreTimerMap_.erase(key);
}

litebus::Future<SyncResult> ObserverActor::BusProxySyncer()
{
    GetOption opts;
    opts.prefix = true;
    YRLOG_INFO("start to sync key({}).", BUSPROXY_PATH_PREFIX);
    return metaStorageAccessor_->GetMetaClient()
        ->Get(BUSPROXY_PATH_PREFIX, opts)
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnBusProxySyncer, std::placeholders::_1));
}

litebus::Future<SyncResult> ObserverActor::OnBusProxySyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    std::vector<WatchEvent> events;
    auto syncResult = OnSyncer(getResponse, events, BUSPROXY_PATH_PREFIX);
    if (syncResult.status.IsError() || events.empty()) {
        return syncResult;
    }
    // clear all cache
    proxyView_->ClearProxyClient();
    localSchedulerView_->Clear();
    UpdateProxyEvent(events);
    return syncResult;
}

litebus::Future<SyncResult> ObserverActor::InstanceInfoSyncer()
{
    GetOption opts;
    opts.prefix = true;
    YRLOG_INFO("start to sync key({}).", INSTANCE_ROUTE_PATH_PREFIX);
    return metaStorageAccessor_->GetMetaClient()
        ->Get(INSTANCE_ROUTE_PATH_PREFIX, opts)
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnInstanceInfoSyncer, std::placeholders::_1));
}

litebus::Future<SyncResult> ObserverActor::OnInstanceInfoSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    std::vector<WatchEvent> events;
    auto syncResult = OnSyncer(getResponse, events, INSTANCE_PATH_PREFIX);
    if (syncResult.status.IsError() || events.empty()) {
        return syncResult;
    }
    std::vector<WatchEvent> remoteWatchRouteEvents;
    std::vector<resource_view::RouteInfo> localWatchRouteInfo;
    std::set<std::string> etcdRemoteSet;

    for (auto event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto keyInfo = ParseInstanceKey(eventKey);
        resource_view::InstanceInfo instanceInfo;
        resource_view::RouteInfo routeInfo;
        if (!TransToInstanceInfoFromJson(instanceInfo, event.kv.value())) {
            YRLOG_ERROR("failed to trans to instanceInfo from json string, instance({})", keyInfo.instanceID);
            continue;
        }
        TransToRouteInfoFromInstanceInfo(instanceInfo, routeInfo);
        if (routeInfo.functionproxyid() != nodeID_) {  // owner is not self_ need to be updated
            etcdRemoteSet.emplace(keyInfo.instanceID);
            if (!functionsystem::NeedUpdateRouteState(static_cast<InstanceState>(routeInfo.instancestatus().code()),
                                                      isMetaStoreEnabled_)) {
                continue;  // ignore non-route info
            }
            remoteWatchRouteEvents.emplace_back(event);
            continue;
        }
        // owner is self, need to compare with cache
        localWatchRouteInfo.emplace_back(routeInfo);
    }

    // not found in etcd
    for (auto info : instanceInfoMap_) {
        if (etcdRemoteSet.find(info.first) != etcdRemoteSet.end()) {
            continue;
        }

        // owner is not self_, need to be deleted
        if (info.second.functionproxyid() != nodeID_) {
            auto &instanceInfo = info.second;
            auto routeKey = GenInstanceRouteKey(instanceInfo.instanceid());
            KeyValue kv;
            kv.set_key(routeKey);
            kv.set_mod_revision(syncResult.revision);
            WatchEvent event{ .eventType = EVENT_TYPE_DELETE, .kv = kv, .prevKv = {} };
            remoteWatchRouteEvents.emplace_back(event);
            YRLOG_DEBUG("need to delete instance {}, which is not in etcd and belong to {}", instanceInfo.instanceid(),
                        info.second.functionproxyid());
        } else if (!IsLowReliabilityInstance(info.second)
                   && static_cast<InstanceState>(info.second.instancestatus().code()) != InstanceState::SCHEDULING) {
            if (!functionsystem::NeedUpdateRouteState(static_cast<InstanceState>(info.second.instancestatus().code()),
                                                      isMetaStoreEnabled_)) {
                continue;  // ignore non-route info
            }

            // owner is self_, put into etcd
            YRLOG_DEBUG("instance({}) isn't exist in meta-store, put instance", info.first);
            // this key doesn't exist in etcd anymore, putting it again will reset etcd's key version to 1
            info.second.set_version(1);
            PutInstance(info.second, true);
        }
    }
    UpdateInstanceRouteEvent(remoteWatchRouteEvents, true);

    for (auto instance : localWatchRouteInfo) {  // check and update local instance info
        instanceInfoSyncerCbFunc_(instance);
    }
    return syncResult;
}

litebus::Future<SyncResult> ObserverActor::PartialInstanceInfoSyncer(const std::string &instanceID)
{
    YRLOG_INFO("start to sync key({}).", GenInstanceRouteKey(instanceID));
    return metaStorageAccessor_->GetMetaClient()
        ->Get(GenInstanceRouteKey(instanceID), {})
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnPartialInstanceInfoSyncer, std::placeholders::_1, instanceID));
}

litebus::Future<SyncResult> ObserverActor::OnPartialInstanceInfoSyncer(const std::shared_ptr<GetResponse> &getResponse,
                                                                       const std::string &instanceID)
{
    std::vector<WatchEvent> events;
    auto syncResult = OnSyncer(getResponse, events, GenInstanceRouteKey(instanceID));
    if (syncResult.status.IsError() || events.size() > 1) {
        return syncResult;
    }

    auto localInstance = instanceInfoMap_.find(instanceID);
    if (events.empty() && localInstance == instanceInfoMap_.end()) {
        // don't exist in meta-store and local, skip
        return syncResult;
    }

    // 1. instance doesn't exist in etcd
    // (1) but exists in local cache
    if (events.empty() && localInstance != instanceInfoMap_.end()) {
        // instance not on current node, update cache with etcd, delete cache
        if (localInstance->second.functionproxyid() != nodeID_) {
            YRLOG_DEBUG("need to delete instance {}, which is not in etcd and belong to {}", instanceID,
                        localInstance->second.functionproxyid());
            KeyValue kv;
            kv.set_key(GenInstanceRouteKey(instanceID));
            kv.set_mod_revision(syncResult.revision);
            WatchEvent event{ .eventType = EVENT_TYPE_DELETE, .kv = kv, .prevKv = {} };
            UpdateInstanceRouteEvent({ event }, true);
            return syncResult;
        }

        // (2) instance is on current node, update etcd if needed
        if (!IsLowReliabilityInstance(localInstance->second)
            && static_cast<InstanceState>(localInstance->second.instancestatus().code()) != InstanceState::SCHEDULING) {
            YRLOG_DEBUG("instance({}) isn't exist in meta-store, put instance", localInstance->first);
            // this key doesn't exist in etcd anymore, putting it agent will reset etcd's key version to 1
            localInstance->second.set_version(1);
            PutInstance(localInstance->second, true);
        }
        return syncResult;
    }
    auto eventKey = TrimKeyPrefix(events.at(0).kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
    auto keyInfo = ParseInstanceKey(eventKey);
    resource_view::InstanceInfo instanceInfo;
    resource_view::RouteInfo routeInfo;
    if (!TransToInstanceInfoFromJson(instanceInfo, events.at(0).kv.value())) {
        YRLOG_ERROR("failed to trans to instanceInfo from json string, instance({})", keyInfo.instanceID);
        return syncResult;
    }
    TransToRouteInfoFromInstanceInfo(instanceInfo, routeInfo);

    // 2. if instance exists in etcd
    // (1) instance not on current node, update cache with etcd
    if (routeInfo.functionproxyid() != nodeID_) {
        if (functionsystem::NeedUpdateRouteState(static_cast<InstanceState>(routeInfo.instancestatus().code()),
                                                 isMetaStoreEnabled_)) {
            UpdateInstanceRouteEvent(events, true);
        }
        return syncResult;
    }

    // (2) owner is self, compare local cache
    instanceInfoSyncerCbFunc_(routeInfo);
    return syncResult;
}

litebus::Future<SyncResult> ObserverActor::FunctionMetaSyncer()
{
    GetOption opts;
    opts.prefix = true;
    YRLOG_INFO("start to sync key({}).", FUNC_META_PATH_PREFIX);
    return metaStorageAccessor_->GetMetaClient()
        ->Get(FUNC_META_PATH_PREFIX, opts)
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnFunctionMetaSyncer, std::placeholders::_1));
}

litebus::Future<SyncResult> ObserverActor::OnFunctionMetaSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    std::vector<WatchEvent> events;
    auto syncResult = OnSyncer(getResponse, events, FUNC_META_PATH_PREFIX);
    if (syncResult.status.IsError() || events.empty()) {
        return syncResult;
    }
    std::unordered_set<std::string> etcdRemoteSet;
    for (const auto &event : events) {
        auto eventKey = TrimKeyPrefix(event.kv.key(), metaStorageAccessor_->GetMetaClient()->GetTablePrefix());
        auto funcKey = GetFuncKeyFromFuncMetaPath(eventKey);
        if (funcKey.empty()) {
            YRLOG_WARN("function key is empty, path: {}", eventKey);
            continue;
        }
        etcdRemoteSet.emplace(funcKey);
    }
    std::unordered_map<std::string, FunctionMeta> needDeleteFuncMetaMap;
    for (auto it = funcMetaMap_.begin(); it != funcMetaMap_.end();) {
        auto localIter = localFuncMetaSet_.find(it->first);
        auto remoteIter = etcdRemoteSet.find(it->first);
        if (localIter == localFuncMetaSet_.end() && remoteIter == etcdRemoteSet.end()) {
            YRLOG_INFO("clear funcMeta({})", it->first);
            needDeleteFuncMetaMap[it->first] = it->second;
            it = funcMetaMap_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = systemFuncMetaMap_.begin(); it != systemFuncMetaMap_.end();) {
        auto localIter = localFuncMetaSet_.find(it->first);
        auto remoteIter = etcdRemoteSet.find(it->first);
        if (localIter == localFuncMetaSet_.end() && remoteIter == etcdRemoteSet.end()) {
            YRLOG_INFO("clear system funcMeta({})", it->first);
            needDeleteFuncMetaMap[it->first] = it->second;
            it = systemFuncMetaMap_.erase(it);
        } else {
            ++it;
        }
    }
    if (updateFuncMetasFunc_ != nullptr) {
        updateFuncMetasFunc_(false, needDeleteFuncMetaMap);
    }
    UpdateFuncMetaEvent(events);
    return syncResult;
}

SyncResult ObserverActor::OnSyncer(const std::shared_ptr<GetResponse> &getResponse, std::vector<WatchEvent> &events,
                                   std::string prefixKey)
{
    if (getResponse->status.IsError()) {
        YRLOG_INFO("failed to get key({}) from meta storage", prefixKey);
        return SyncResult{ getResponse->status, 0 };
    }
    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with key({}) from meta storage, revision is {}", prefixKey,
                   getResponse->header.revision);
        return SyncResult{ Status::OK(), getResponse->header.revision + 1 };
    }

    for (auto &kv : getResponse->kvs) {
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} };
        events.emplace_back(event);
    }
    return SyncResult{ Status::OK(), getResponse->header.revision + 1 };
}

litebus::Future<resource_view::InstanceInfo> ObserverActor::GetInstanceRouteInfo(const std::string &instanceID)
{
    if (instanceInfoMap_.find(instanceID) != instanceInfoMap_.end()) {
        YRLOG_DEBUG("instance({}) existed, no need to get from meta store", instanceID);
        return instanceInfoMap_[instanceID];
    }

    litebus::Promise<resource_view::InstanceInfo> promise;
    if (instanceWatchers_.find(instanceID) != instanceWatchers_.end()) {
        YRLOG_ERROR("instance({}) watcher already existed, no need to check meta store, instance doesn't exist",
                    instanceID);
        return litebus::Future<resource_view::InstanceInfo>(litebus::Status(-1));
    }

    return metaStorageAccessor_->GetMetaClient()
        ->Get(GenInstanceRouteKey(instanceID), {})
        .Then(litebus::Defer(GetAID(), &ObserverActor::OnGetInstanceFromMetaStore, std::placeholders::_1, instanceID));
}

litebus::Future<resource_view::InstanceInfo> ObserverActor::OnGetInstanceFromMetaStore(
    const litebus::Future<std::shared_ptr<GetResponse>> &getResponse, const std::string &instanceID)
{
    if (getResponse.IsError() || getResponse.Get() == nullptr || getResponse.Get()->kvs.empty()) {
        YRLOG_ERROR("failed to get instance({}) from meta store", instanceID);
        return litebus::Future<resource_view::InstanceInfo>(litebus::Status(-1));
    }
    resource_view::RouteInfo routeInfo;
    if (!TransToRouteInfoFromJson(routeInfo, getResponse.Get()->kvs.front().value())) {
        YRLOG_ERROR("failed to trans to routeInfo from json string, instance({})", instanceID);
        return litebus::Future<resource_view::InstanceInfo>(litebus::Status(-1));
    }

    resource_view::InstanceInfo instanceInfo;
    if (instanceInfoMap_.find(instanceID) != instanceInfoMap_.end()) {
        // info in instanceInfoMap_ may be full InstanceInfo instead of RouteInfo, only update
        YRLOG_DEBUG("find and update instance({})", instanceID);
        instanceInfo = instanceInfoMap_[instanceID];
    }
    YRLOG_INFO("get instance({}) info from meta-store", instanceID);
    TransToInstanceInfoFromRouteInfo(routeInfo, instanceInfo);
    (*instanceInfo.mutable_extensions())[INSTANCE_MOD_REVISION] =
        std::to_string(getResponse.Get()->kvs.front().mod_revision());
    PutInstanceEvent(instanceInfo, true, getResponse.Get()->kvs.front().mod_revision());
    return instanceInfo;
}

void ObserverActor::WatchInstance(const std::string &instanceID, int64_t revision)
{
    if (!isPartialWatchInstances_) {
        return;
    }

    if (instanceWatchers_.find(instanceID) != instanceWatchers_.end()) {
        YRLOG_DEBUG("instance({}) has already been watched", instanceID);
        return;
    }
    // to avoid duplicate watch
    instanceWatchers_[instanceID] = nullptr;
    auto key = GenInstanceRouteKey(instanceID);
    YRLOG_INFO("Register watch for instance: {}, key: {}", instanceID, key);
    auto watchOpt = WatchOption{ false, false, revision, true };
    auto partialInstanceInfoSyncer = [aid(GetAID()), instanceID]() -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &ObserverActor::PartialInstanceInfoSyncer, instanceID);
    };
    (void)metaStorageAccessor_
        ->RegisterObserver(
            key, watchOpt,
            [aid(GetAID())](const std::vector<WatchEvent> &events, bool synced) {
                auto respCopy = events;
                litebus::Async(aid, &ObserverActor::UpdateInstanceRouteEvent, respCopy, synced);
                return true;
            },
            partialInstanceInfoSyncer)
        .After(WATCH_TIMEOUT_MS,
               [instanceID, key](const litebus::Future<std::shared_ptr<Watcher>> &watcher)
                   -> litebus::Future<std::shared_ptr<Watcher>> {
                   YRLOG_ERROR("failed to register watch for instance: {}, key: {}", instanceID, key);
                   return watcher;
               })
        .OnComplete(litebus::Defer(GetAID(), &ObserverActor::OnWatchInstance, instanceID, std::placeholders::_1));
}

void ObserverActor::OnWatchInstance(const std::string &instanceID,
                                    const litebus::Future<std::shared_ptr<Watcher>> &watcher)
{
    if (watcher.IsError() || watcher.Get() == nullptr) {
        YRLOG_ERROR("failed to watch instance: {}", instanceID);
        instanceWatchers_.erase(instanceID);
        return;
    }
    YRLOG_INFO("success to watch instance: {}", instanceID);
    instanceWatchers_[instanceID] = watcher.Get();
}

litebus::Future<resource_view::InstanceInfo> ObserverActor::GetAndWatchInstance(const std::string &instanceID)
{
    if (!isPartialWatchInstances_) {
        if (instanceInfoMap_.find(instanceID) != instanceInfoMap_.end()) {
            YRLOG_DEBUG("find existed instance({})", instanceID);
            resource_view::InstanceInfo instanceInfo;
            return instanceInfoMap_[instanceID];
        }
        return litebus::Future<resource_view::InstanceInfo>(litebus::Status(-1));
    }

    litebus::Promise<resource_view::InstanceInfo> promise;
    GetInstanceRouteInfo(instanceID)
        .OnComplete([instanceID, aid(GetAID()), promise](const litebus::Future<resource_view::InstanceInfo> &future) {
            if (future.IsError()) {
                promise.SetFailed(future.GetErrorCode());
                YRLOG_ERROR("failed to GetInstanceRouteInfo for {}, don't need to watch instance", instanceID);
                return;
            }
            promise.SetValue(future.Get());
            litebus::Async(aid, &ObserverActor::WatchInstance, instanceID,
                           GetModRevisionFromInstanceInfo(future.Get()));
        });
    return promise.GetFuture();
}

void ObserverActor::CancelWatchInstance(const std::string &instanceID)
{
    if (!isPartialWatchInstances_) {
        return;
    }

    if (auto iter = instanceWatchers_.find(instanceID); iter != instanceWatchers_.end()) {
        YRLOG_INFO("instance({}) watcher is canceled", instanceID);
        if (iter->second != nullptr) {
            iter->second->Close();
        }
        instanceWatchers_.erase(instanceID);
    }
}

}  // namespace functionsystem::function_proxy
