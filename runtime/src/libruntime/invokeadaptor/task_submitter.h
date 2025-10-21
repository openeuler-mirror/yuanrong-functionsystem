/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#pragma once
#include <unordered_map>
#include <unordered_set>

#include "src/dto/acquire_options.h"
#include "src/libruntime/invokeadaptor/instance_manager.h"
#include "src/libruntime/invokeadaptor/request_manager.h"
#include "src/libruntime/invokeadaptor/request_queue.h"
#include "src/libruntime/invokeadaptor/task_scheduler.h"
#include "src/libruntime/objectstore/memory_store.h"
#include "src/libruntime/utils/utils.h"
#include "src/utility/time_measurement.h"
namespace YR {
namespace Libruntime {
using KillFunc = std::function<ErrorInfo(const std::string &, const std::string &, int32_t)>;
using CancelFunc = std::function<void(const std::string &, const std::string &, int32_t)>;
using YR::utility::TimeMeasurement;
class TaskSubmitter : public std::enable_shared_from_this<TaskSubmitter> {
public:
    TaskSubmitter() = default;
    TaskSubmitter(std::shared_ptr<LibruntimeConfig> config, std::shared_ptr<MemoryStore> store,
                  std::shared_ptr<FSClient> client, std::shared_ptr<RequestManager> reqMgr, CancelFunc cancelFunc);
    virtual ~TaskSubmitter();
    void SubmitFunction(std::shared_ptr<InvokeSpec> spec);
    void ScheduleFunction(const RequestResource &resource);
    void HandleInvokeNotify(const NotifyRequest &req, const ErrorInfo &err);
    void HandleFailInvokeNotify(const NotifyRequest &req, std::shared_ptr<InvokeSpec> spec,
                                const RequestResource &resource, const ErrorInfo &err);
    void HandleSuccessInvokeNotify(const NotifyRequest &req, std::shared_ptr<InvokeSpec> spec,
                                   const RequestResource &resource);
    bool NeedRetry(const ErrorInfo &errInfo, const std::shared_ptr<InvokeSpec> spec, bool &isConsumeRetryTime);
    bool NeedRetryCreate(const ErrorInfo &errInfo);
    ErrorInfo CancelStatelessRequest(const std::vector<std::string> &objids, const KillFunc &killCallBack, bool isForce,
                                     bool isRecursive);
    void Finalize(void);
    std::vector<std::string> GetInstanceIds();
    std::vector<std::string> GetCreatingInsIds();
    void UpdateConfig();
    void Init();

private:
    bool CancelAndScheOtherRes(const RequestResource &resource);
    void ScheduleIns(const RequestResource &resource, const ErrorInfo &err, bool isRemainIns);
    bool HandleFailInvokeIsDelayScaleDown(const NotifyRequest &req, const ErrorInfo &err);
    void DeleteInsCallback(const std::string &instanceId);
    bool ScheduleRequest(const RequestResource &resource, std::shared_ptr<BaseQueue> requestQueue);
    void SendInvokeReq(const RequestResource &resource, std::shared_ptr<InvokeSpec> invokeSpec);
    std::shared_ptr<LibruntimeConfig> libRuntimeConfig;
    std::atomic<bool> runFlag{true};
    mutable absl::Mutex reqMtx_;
    mutable absl::Mutex cancelTimerMtx_;
    std::shared_ptr<MemoryStore> memoryStore;
    std::shared_ptr<FSClient> fsClient;
    std::shared_ptr<RequestManager> requestManager;
    int recycleTimeMs;
    std::unordered_map<libruntime::ApiType, std::shared_ptr<InsManager>> insManagers;
    std::unordered_map<std::string, TimeMeasurement> invokeCostMap ABSL_GUARDED_BY(invokeCostMtx_);
    mutable absl::Mutex invokeCostMtx_;
    std::unordered_map<RequestResource, std::shared_ptr<BaseQueue>, HashFn> waitScheduleReqMap_
        ABSL_GUARDED_BY(reqMtx_);
    std::unordered_map<RequestResource, std::shared_ptr<TaskScheduler>, HashFn> taskSchedulerMap_
        ABSL_GUARDED_BY(reqMtx_);
    CancelFunc cancelCb;
};

}  // namespace Libruntime
}  // namespace YR