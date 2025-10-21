/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
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

#include <boost/format.hpp>
#include <json.hpp>
#include <msgpack.hpp>
#include <optional>

#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include "src/dto/config.h"
#include "src/dto/constant.h"
#include "src/libruntime/fsclient/protobuf/core_service.grpc.pb.h"
#include "src/libruntime/invokeadaptor/normal_instance_manager.h"
#include "src/libruntime/utils/constants.h"
#include "src/libruntime/utils/exception.h"
#include "src/libruntime/utils/serializer.h"
#include "src/proto/libruntime.pb.h"
#include "src/utility/id_generator.h"
#include "src/utility/timer_worker.h"
#include "task_submitter.h"
namespace YR {
namespace Libruntime {
const std::string INSTANCE_REQUIREMENT_RESOURKEY = "resourcesData";
const std::string INSTANCE_REQUIREMENT_INSKEY = "designateInstanceID";
const std::string INSTANCE_REQUIREMENT_POOLLABELKEY = "poolLabel";
const int64_t BEFOR_RETAIN_TIME = 30;  // millisecond
const int SECONDS_TO_MILLISECONDS_UNIT = 1000;  // millisecond
const int64_t IDLE_TIMER_INTERNAL = 10;
const int DEFALUT_CANCEL_DELAY_TIME = 5; // second
const std::string DS_OBJECTID_SEPERATOR = ";";
using namespace YR::utility;
using json = nlohmann::json;

ErrorInfo PackageNotifyErr(const NotifyRequest &notifyReq, bool isCreate)
{
    std::vector<YR::Libruntime::StackTraceInfo> stackTraceInfos = GetStackTraceInfos(notifyReq);
    YRLOG_DEBUG("get stackTraceInfos from notify request in task_submitter package size: {}", stackTraceInfos.size());
    ErrorInfo errInfo(static_cast<ErrorCode>(notifyReq.code()), ModuleCode::CORE, notifyReq.message(), isCreate,
                      stackTraceInfos);
    return errInfo;
}

TaskSubmitter::TaskSubmitter(std::shared_ptr<LibruntimeConfig> config, std::shared_ptr<MemoryStore> store,
                             std::shared_ptr<FSClient> client, std::shared_ptr<RequestManager> reqMgr,
                             CancelFunc cancelFunc)
    : libRuntimeConfig(config), memoryStore(store), fsClient(client), requestManager(reqMgr), cancelCb(cancelFunc)
{
    this->Init();
}

void TaskSubmitter::Init()
{
    auto normalInsManager =
        std::make_shared<NormalInsManager>(std::bind(&TaskSubmitter::ScheduleIns, this, std::placeholders::_1,
                                                     std::placeholders::_2, std::placeholders::_3),
                                           fsClient, memoryStore, requestManager, libRuntimeConfig);
    normalInsManager->SetDeleleInsCallback(std::bind(&TaskSubmitter::DeleteInsCallback, this, std::placeholders::_1));
    insManagers[libruntime::ApiType::Function] = normalInsManager;
    this->UpdateConfig();
}

void TaskSubmitter::UpdateConfig()
{
    int recycleTime = libRuntimeConfig->recycleTime;
    if (libRuntimeConfig->recycleTime <= 0) {
        YRLOG_WARN("recycle time is invalid, expect > 0,  actual {}", libRuntimeConfig->recycleTime);
        recycleTime = DEFAULT_RECYCLETIME;
    }
    this->recycleTimeMs = recycleTime * S_TO_MS;
    insManagers[libruntime::ApiType::Function]->UpdateConfig(this->recycleTimeMs);
}

TaskSubmitter::~TaskSubmitter()
{
    Finalize();
}

void TaskSubmitter::HandleInvokeNotify(const NotifyRequest &req, const ErrorInfo &err)
{
    if (!runFlag) {
        return;
    }

    ErrorInfo errInfo(static_cast<ErrorCode>(req.code()), req.message());
    if (errInfo.Finalized()) {
        return;
    }
    std::string requestId = req.requestid();
    auto [rawRequestId, seq] = YR::utility::IDGenerator::DecodeRawRequestId(requestId);
    std::shared_ptr<InvokeSpec> spec = requestManager->GetRequest(rawRequestId);
    if (spec == nullptr) {
        YRLOG_WARN(
            "request id : {} did not exit in request manager, may be the invoke request has been canceled or finished.",
            requestId);
        return;
    }
    if (spec->IsStaleDuplicateNotify(seq)) {
        return;
    }
    insManagers[spec->functionMeta.apiType]->DecreaseUnfinishReqNum(spec, HandleFailInvokeIsDelayScaleDown(req, err));
    RequestResource resource = GetRequestResource(spec);
    if (req.code() != common::ERR_NONE) {
        HandleFailInvokeNotify(req, spec, resource, err);
    } else {
        HandleSuccessInvokeNotify(req, spec, resource);
    }
    {
        absl::ReaderMutexLock lock(&invokeCostMtx_);
        if (invokeCostMap.find(spec->invokeInstanceId) == invokeCostMap.end()) {
            return;
        }
    }
    absl::WriterMutexLock lock(&invokeCostMtx_);
    if (invokeCostMap.find(spec->invokeInstanceId) != invokeCostMap.end()) {
        invokeCostMap[spec->invokeInstanceId].StopTimer(requestId, req.code() == common::ERR_NONE);
    }
}

bool TaskSubmitter::HandleFailInvokeIsDelayScaleDown(const NotifyRequest &req, const ErrorInfo &err)
{
    // If error code is less than 2000, it means user operation error, and the instance is normal,
    // so we can hold the instance, delay scale down.
    // Otherwise, invoke failure is either because of fault or only user code error,
    // it cannot be distinguished here, so just scale down the instance with no delay,
    // to avoid instance/node fault.
    YRLOG_INFO("check if invoke is abnormal notify request code {} requestid {}", req.code(), req.requestid());
    if (req.code() == common::ErrorCode::ERR_INSTANCE_NOT_FOUND ||
        req.code() == common::ErrorCode::ERR_INSTANCE_EXITED || req.code() == common::ErrorCode::ERR_INSTANCE_EVICTED) {
        return false;
    }
    if (req.code() == common::ErrorCode::ERR_INNER_SYSTEM_ERROR && err.IsTimeout()) {
        return true;
    }
    if (req.code() < ::common::ErrorCode::ERR_USER_CODE_LOAD) {
        return true;
    }
    return false;
}

void TaskSubmitter::HandleFailInvokeNotify(const NotifyRequest &req, const std::shared_ptr<InvokeSpec> spec,
                                           const RequestResource &resource, const ErrorInfo &err)
{
    insManagers[spec->functionMeta.apiType]->ScaleDown(spec, HandleFailInvokeIsDelayScaleDown(req, err));
    auto isCreate = spec->invokeType == libruntime::InvokeType::CreateInstanceStateless ||
                    spec->invokeType == libruntime::InvokeType::CreateInstance;
    auto errInfo = PackageNotifyErr(req, isCreate);
    bool isConsumeRetryTime = false;
    if (NeedRetry(errInfo, spec, isConsumeRetryTime)) {
        spec->IncrementRequestID(spec->requestInvoke->Mutable());
        YRLOG_ERROR(
            "normal invoke requet fail, need retry, raw request id is {}, code is: {}, trace id is {}, seq is {}, "
            "complete request id is {}",
            req.requestid(), req.code(), spec->traceId, spec->seq, spec->requestInvoke->Mutable().requestid());
        if (isConsumeRetryTime) {
            spec->ConsumeRetryTime();
            YRLOG_DEBUG("consumed invoke retry time to {}, req id is {}", spec->opts.retryTimes, req.requestid());
        }
        std::shared_ptr<BaseQueue> requestQueue;
        {
            absl::ReaderMutexLock lock(&reqMtx_);
            requestQueue = waitScheduleReqMap_[resource];
        }
        {
            std::lock_guard<std::mutex> lockGuard(requestQueue->atomicMtx);
            requestQueue->Push(spec);
        }
    } else {
        YRLOG_ERROR(
            "normal invoke requet fail, don't need retry, raw request id is {}, code is: {}, trace id is {}, seq is "
            "{}, complete request id is {}",
            req.requestid(), req.code(), spec->traceId, spec->seq, spec->requestInvoke->Mutable().requestid());
        if (this->libRuntimeConfig->inCluster) {
            std::vector<std::string> dsObjs;
            for (auto it = spec->returnIds.begin(); it != spec->returnIds.end(); ++it) {
                if (!it->id.empty()) {
                    dsObjs.emplace_back(it->id);
                }
            }
        }
        auto ids = this->memoryStore->UnbindObjRefInReq(spec->requestId);
        auto errorInfo = this->memoryStore->DecreGlobalReference(ids);
        if (!errorInfo.OK()) {
            YRLOG_WARN("failed to decrease obj ref [{},...] by requestid {}. Code: {}, MCode: {}, Msg: {}", ids[0],
                       spec->requestId, errorInfo.Code(), errorInfo.MCode(), errorInfo.Msg());
        }
        this->memoryStore->SetError(spec->returnIds, errInfo);
        (void)requestManager->RemoveRequest(spec->requestId);
    }
    absl::ReaderMutexLock lock(&reqMtx_);
    if (taskSchedulerMap_.find(resource) != taskSchedulerMap_.end()) {
        taskSchedulerMap_[resource]->Notify();
    }
}

void TaskSubmitter::HandleSuccessInvokeNotify(const NotifyRequest &req, const std::shared_ptr<InvokeSpec> spec,
                                              const RequestResource &resource)
{
    YRLOG_DEBUG("handle nomarl invoke finish, request id: {}, trace id: {}", req.requestid(), spec->traceId);
    insManagers[spec->functionMeta.apiType]->ScaleDown(spec, true);
    (void)requestManager->RemoveRequest(spec->requestId);
    int smallObjSize = req.smallobjects_size();
    size_t curPos = 0;
    std::vector<std::string> dsObjs;

    if (smallObjSize <= 0) {
        for (size_t j = 0; j < spec->returnIds.size(); j++) {
            dsObjs.emplace_back(spec->returnIds[j].id);
        }
    } else {
        for (int i = 0; i < smallObjSize; i++) {
            auto &smallObj = req.smallobjects(i);
            const std::string &bufStr = smallObj.value();
            std::shared_ptr<Buffer> buf = std::make_shared<NativeBuffer>(bufStr.size());
            buf->MemoryCopy(bufStr.data(), bufStr.size());
            YRLOG_DEBUG("set small obj into memory store, obj id: {}, req id: {}, instace id: {}", smallObj.id(),
                        req.requestid(), spec->invokeInstanceId);
            memoryStore->Put(buf, smallObj.id(), {}, false);

            for (size_t j = curPos; j < spec->returnIds.size(); j++) {
                if (spec->returnIds[j].id == smallObj.id()) {
                    curPos++;
                    break;
                }
                dsObjs.emplace_back(spec->returnIds[j].id);
                curPos++;
            }
        }
    }
    if (this->libRuntimeConfig->inCluster && !dsObjs.empty()) {
        this->memoryStore->IncreDSGlobalReference(dsObjs);
    }
    this->memoryStore->SetReady(spec->returnIds);
    auto ids = this->memoryStore->UnbindObjRefInReq(spec->requestId);
    auto errorInfo = this->memoryStore->DecreGlobalReference(ids);
    if (!errorInfo.OK()) {
        YRLOG_WARN("failed to decrease obj ref [{},...] by requestid {}. Code: {}, MCode: {}, Msg: {}", ids[0],
                   spec->requestId, errorInfo.Code(), errorInfo.MCode(), errorInfo.Msg());
    }
    absl::ReaderMutexLock lock(&reqMtx_);
    if (taskSchedulerMap_.find(resource) != taskSchedulerMap_.end()) {
        taskSchedulerMap_[resource]->Notify();
    }
}

void TaskSubmitter::SubmitFunction(std::shared_ptr<InvokeSpec> spec)
{
    YRLOG_DEBUG("start submit stateless function, req id is {}, return obj id is {}, trace id is {}", spec->requestId,
                spec->returnIds[0].id, spec->traceId);
    RequestResource resource = GetRequestResource(spec);
    std::shared_ptr<BaseQueue> queue;
    std::shared_ptr<TaskScheduler> taskScheduler;
    {
        absl::ReaderMutexLock lock(&reqMtx_);
        if (waitScheduleReqMap_.find(resource) != waitScheduleReqMap_.end()) {
            queue = waitScheduleReqMap_[resource];
        }
        if (taskSchedulerMap_.find(resource) != taskSchedulerMap_.end()) {
            taskScheduler = taskSchedulerMap_[resource];
        }
    }
    if (queue == nullptr && taskScheduler == nullptr) {
        absl::WriterMutexLock lock(&reqMtx_);
        if (waitScheduleReqMap_.find(resource) == waitScheduleReqMap_.end()) {
            queue = std::make_shared<PriorityQueue>();
            auto weakPtr = weak_from_this();
            auto cb = [weakPtr, resource, this]() {
                auto thisPtr = weakPtr.lock();
                if (!thisPtr) {
                    return;
                }
                ScheduleFunction(resource);
            };
            taskScheduler = std::make_shared<TaskScheduler>(cb);
            taskScheduler->Run();
            waitScheduleReqMap_[resource] = queue;
            taskSchedulerMap_[resource] = taskScheduler;
        } else {
            queue = waitScheduleReqMap_[resource];
            taskScheduler = taskSchedulerMap_[resource];
        }
    }
    {
        std::lock_guard<std::mutex> lockGuard(queue->atomicMtx);
        queue->Push(spec);
    }
    taskScheduler->Notify();
}

void TaskSubmitter::ScheduleIns(const RequestResource &resource, const ErrorInfo &errInfo, bool isRemainIns)
{
    if (errInfo.OK()) {
        absl::ReaderMutexLock lock(&reqMtx_);
        if (taskSchedulerMap_.find(resource) != taskSchedulerMap_.end()) {
            taskSchedulerMap_[resource]->Notify();
        }
        return;
    }
    if (NeedRetryCreate(errInfo)) {
        YRLOG_INFO("start retry create task instance, code: {}, msg: {}", errInfo.Code(), errInfo.Msg());
        absl::ReaderMutexLock lock(&reqMtx_);
        if (taskSchedulerMap_.find(resource) != taskSchedulerMap_.end()) {
            taskSchedulerMap_[resource]->Notify();
        }
        return;
    }
    std::shared_ptr<BaseQueue> requestQueue;
    std::shared_ptr<TaskScheduler> taskScheduler;
    {
        absl::ReaderMutexLock lock(&reqMtx_);
        if (waitScheduleReqMap_.find(resource) == waitScheduleReqMap_.end()) {
            return;
        }
        requestQueue = waitScheduleReqMap_[resource];
        taskScheduler = taskSchedulerMap_[resource];
    }
    // If there are still other instances existing or being created under resource, equals the
    // isRemainIns value is true, then all requests under that resource should not be set as failed
    if (!isRemainIns) {
        std::lock_guard<std::mutex> lockGuard(requestQueue->atomicMtx);
        for (; !requestQueue->Empty(); requestQueue->Pop()) {
            auto reqId = requestQueue->Top()->requestId;
            std::shared_ptr<InvokeSpec> invokeSpecNeedFailed;
            bool specExits = requestManager->PopRequest(reqId, invokeSpecNeedFailed);
            if (!specExits) {
                continue;
            }
            this->memoryStore->SetError(invokeSpecNeedFailed->returnIds[0].id, errInfo);
        }
    }
    if (taskScheduler) {
        taskScheduler->Notify();
    }
}

void TaskSubmitter::SendInvokeReq(const RequestResource &resource, std::shared_ptr<InvokeSpec> invokeSpec)
{
    YRLOG_DEBUG(
        "start send stateless function invoke req, instance id is: {}, lease id is: {}, req id is: {}, return obj "
        "id is: {}, function name is: {}, trace id is: {}",
        invokeSpec->invokeInstanceId, invokeSpec->invokeLeaseId, invokeSpec->requestId, invokeSpec->returnIds[0].id,
        invokeSpec->functionMeta.funcName, invokeSpec->traceId);

    if (!invokeSpec->opts.device.name.empty()) {
        absl::WriterMutexLock lock(&invokeCostMtx_);
        invokeCostMap[invokeSpec->invokeInstanceId].StartTimer(invokeSpec->requestId);
        YRLOG_DEBUG("start timer for instance: {}, reqID: {}", invokeSpec->invokeInstanceId, invokeSpec->requestId);
    }

    auto weakThis = weak_from_this();
    auto insId = invokeSpec->invokeInstanceId;
    this->fsClient->InvokeAsync(
        invokeSpec->requestInvoke,
        [weakThis, insId](const NotifyRequest &notifyRequest, const ErrorInfo &err) {
            if (auto thisPtr = weakThis.lock(); thisPtr) {
                thisPtr->HandleInvokeNotify(notifyRequest, err);
                if (err.IsTimeout() && thisPtr->cancelCb) {
                    // if timeout,then send cancel req to runtime for erase pending thread
                    YRLOG_DEBUG("start send cancel req to runtime: {} for req: {}", insId, notifyRequest.requestid());
                    thisPtr->cancelCb(insId, notifyRequest.requestid(), libruntime::Signal::ErasePendingThread);
                }
            }
        },
        invokeSpec->opts.timeout);
}

bool TaskSubmitter::ScheduleRequest(const RequestResource &resource, std::shared_ptr<BaseQueue> requestQueue)
{
    std::unique_lock<std::mutex> atomicLock(requestQueue->atomicMtx);
    if (requestQueue->Empty()) {
        return true;
    }
    auto requestId = requestQueue->Top()->requestId;
    auto requestQueueSize = requestQueue->Size();
    YRLOG_DEBUG("current size of request queue is {}, top request id is {}", requestQueueSize, requestId);
    auto invokeSpec = requestManager->GetRequest(requestId);
    if (invokeSpec == nullptr) {
        YRLOG_WARN("The request {} has been cancelled", requestId);
        requestQueue->Pop();
        return false;
    }
    auto [instanceId, leaseId] = insManagers[invokeSpec->functionMeta.apiType]->ScheduleIns(resource);
    if (instanceId.empty()) {
        atomicLock.unlock();
        YRLOG_DEBUG("invoke request {} can not be scheduled, instanceId is empty", requestId);
        bool needCreate = insManagers[invokeSpec->functionMeta.apiType]->ScaleUp(invokeSpec, requestQueueSize);
        return !needCreate;
    }
    requestQueue->Pop();
    atomicLock.unlock();
    invokeSpec->invokeInstanceId = instanceId;
    invokeSpec->invokeLeaseId = leaseId;
    invokeSpec->requestInvoke->Mutable().set_instanceid(instanceId);
    invokeSpec->instanceRoute = memoryStore->GetInstanceRoute(invokeSpec->returnIds[0].id);
    SendInvokeReq(resource, invokeSpec);
    return false;
}

bool TaskSubmitter::CancelAndScheOtherRes(const RequestResource &resource)
{
    absl::ReaderMutexLock lock(&reqMtx_);
    auto it = waitScheduleReqMap_.find(resource);
    if (it == waitScheduleReqMap_.end()) {
        return true;
    }
    if (!it->second->Empty()) {
        return false;
    }
    YRLOG_DEBUG(
        "current resource req queue is empty, try scheduler other resource req. func name is {}, class "
        "name is "
        "{}",
        resource.functionMeta.funcName, resource.functionMeta.className);
    insManagers[resource.functionMeta.apiType]->ScaleCancel(resource, 0, true);
    for (auto &entry : waitScheduleReqMap_) {
        if (waitScheduleReqMap_.find(entry.first) == it) {
            continue;
        }
        if (!entry.second->Empty()) {
            taskSchedulerMap_[entry.first]->Notify();
            return true;
        }
    }
    return true;
}

void TaskSubmitter::ScheduleFunction(const RequestResource &resource)
{
    YRLOG_DEBUG("schedule resource req. func name is {}, class name is {}", resource.functionMeta.funcName,
                resource.functionMeta.className);
    if (!runFlag) {
        return;
    }
    if (CancelAndScheOtherRes(resource)) {
        return;
    }
    std::shared_ptr<BaseQueue> requestQueue;
    {
        absl::ReaderMutexLock lock(&reqMtx_);
        requestQueue = waitScheduleReqMap_[resource];
    }
    while (!requestQueue->Empty()) {
        if (auto finish = ScheduleRequest(resource, requestQueue); finish) {
            break;
        }
    }
}

bool TaskSubmitter::NeedRetryCreate(const ErrorInfo &errInfo)
{
    auto errCode = errInfo.Code();
    if (errCode == ErrorCode::ERR_RESOURCE_NOT_ENOUGH || errCode == ErrorCode::ERR_INNER_COMMUNICATION ||
        errCode == ErrorCode::ERR_REQUEST_BETWEEN_RUNTIME_BUS) {
        return true;
    }
    return false;
}

bool TaskSubmitter::NeedRetry(const ErrorInfo &errInfo, const std::shared_ptr<InvokeSpec> spec,
                              bool &isConsumeRetryTime)
{
    auto errCode = errInfo.Code();
    if (spec->invokeType == libruntime::InvokeType::InvokeFunctionStateless &&
        (errCode == ErrorCode::ERR_INSTANCE_EVICTED || errCode == ErrorCode::ERR_INSTANCE_NOT_FOUND ||
         errCode == ErrorCode::ERR_INSTANCE_EXITED)) {
        isConsumeRetryTime = false;  // the only case to retry without consuming
        return true;
    }

    if (spec->opts.retryTimes <= 0) {
        isConsumeRetryTime = false;
        return false;
    }
    switch (spec->invokeType) {
        case libruntime::InvokeType::InvokeFunctionStateless: {
            static const std::unordered_set<ErrorCode> codesWorthRetry = {
                ErrorCode::ERR_USER_FUNCTION_EXCEPTION, ErrorCode::ERR_REQUEST_BETWEEN_RUNTIME_BUS,
                ErrorCode::ERR_INNER_COMMUNICATION,     ErrorCode::ERR_SHARED_MEMORY_LIMITED,
                ErrorCode::ERR_OPERATE_DISK_FAILED,     ErrorCode::ERR_INSUFFICIENT_DISK_SPACE,
                ErrorCode::ERR_INSTANCE_NOT_FOUND,      ErrorCode::ERR_INSTANCE_EXITED,
                ErrorCode::ERR_INSTANCE_SUB_HEALTH,     ErrorCode::ERR_REQUEST_BETWEEN_RUNTIME_FRONTEND};
            bool isRetryable = (codesWorthRetry.find(errCode) != codesWorthRetry.end());
            if (isRetryable && spec->opts.retryChecker) {
                isConsumeRetryTime = spec->opts.retryChecker(errInfo);
                return isConsumeRetryTime;
            }
            // no retry checker, then retry for all codes worth retrying
            isConsumeRetryTime = isRetryable;
            return isConsumeRetryTime;
        }
        default:
            break;
    }
    return (isConsumeRetryTime = false);
}

ErrorInfo TaskSubmitter::CancelStatelessRequest(const std::vector<std::string> &objids, const KillFunc &killCallBack,
                                                bool isForce, bool isRecursive)
{
    std::unordered_set<std::string> reqIdSet;
    std::unordered_set<std::string> cancelReq;
    ErrorInfo err;
    for (unsigned int i = 0; i < objids.size(); i++) {
        auto requestId = YR::utility::IDGenerator::GetRequestIdFromObj(objids[i]);
        if (!err.OK()) {
            YRLOG_ERROR("failed to get requestID from objID: {}, err: {}", objids[i], err.Msg());
            continue;
        }
        reqIdSet.insert(requestId);
    }

    for (auto &reqId : reqIdSet) {
        auto spec = requestManager->GetRequest(reqId);
        if (spec != nullptr && spec->invokeInstanceId == "") {
            // request exists, but scheduling has not been initiated
            requestManager->RemoveRequest(reqId);
            cancelReq.insert(reqId);
        } else if (isForce && spec != nullptr && spec->invokeInstanceId != "") {
            // request exists, an invoke request has been sent
            RequestResource resource = GetRequestResource(spec);
            if (resource.concurrency > MIN_CONCURRENCY) {
                std::ostringstream oss;
                oss << "request " << reqId
                    << " has been sent to the runtime, and concurrency is greater than 1.Cancellation is not "
                       "supported.";
                return ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, YR::Libruntime::ModuleCode::RUNTIME,
                                 oss.str());
            }
            if (isRecursive) {
                (void)killCallBack(spec->invokeInstanceId, "", libruntime::Signal::Cancel);
            } else {
                (void)killCallBack(spec->invokeInstanceId, "", libruntime::Signal::KillInstance);
            }
            insManagers[spec->functionMeta.apiType]->DelInsInfo(spec->invokeInstanceId, resource);
            requestManager->RemoveRequest(reqId);
            cancelReq.insert(reqId);
        }
    }

    ErrorInfo cancelErr(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, YR::Libruntime::ModuleCode::RUNTIME,
                        "invalid get obj, the obj has been cancelled.");
    for (unsigned int i = 0; i < objids.size(); i++) {
        auto requestId = YR::utility::IDGenerator::GetRequestIdFromObj(objids[i]);
        if (!err.OK()) {
            YRLOG_ERROR("failed to get requestID from objID: {}, err: {}", objids[i], err.Msg());
            continue;
        }
        if (cancelReq.find(requestId) != cancelReq.end()) {
            memoryStore->SetError(objids[i], cancelErr);
        }
    }
    return ErrorInfo();
}

std::vector<std::string> TaskSubmitter::GetInstanceIds()
{
    if (insManagers[libruntime::ApiType::Function]) {
        return insManagers[libruntime::ApiType::Function]->GetInstanceIds();
    }
    return std::vector<std::string>();
}

std::vector<std::string> TaskSubmitter::GetCreatingInsIds()
{
    if (insManagers[libruntime::ApiType::Function]) {
        return insManagers[libruntime::ApiType::Function]->GetCreatingInsIds();
    }
    return std::vector<std::string>();
}

void TaskSubmitter::Finalize()
{
    if (!runFlag) {
        return;
    }
    runFlag = false;
    {
        absl::ReaderMutexLock lock(&reqMtx_);
        for (auto &pair : taskSchedulerMap_) {
            pair.second->Stop();
        }
    }
    {
        absl::WriterMutexLock lock(&reqMtx_);
        taskSchedulerMap_.clear();
        waitScheduleReqMap_.clear();
    }
    for (auto &pair : insManagers) {
        pair.second->Stop();
    }
}

void TaskSubmitter::DeleteInsCallback(const std::string &instanceId)
{
    if (fsClient) {
        fsClient->RemoveInsRtIntf(instanceId);
    }
}
}  // namespace Libruntime
}  // namespace YR