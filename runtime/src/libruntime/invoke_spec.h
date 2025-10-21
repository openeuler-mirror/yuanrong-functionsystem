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
#include <ctime>
#include <future>
#include <vector>

#include <json.hpp>

#include "src/dto/acquire_options.h"
#include "src/dto/config.h"
#include "src/dto/data_object.h"
#include "src/dto/invoke_arg.h"
#include "src/dto/invoke_options.h"
#include "src/libruntime/fsclient/fs_intf.h"
#include "src/libruntime/fsclient/protobuf/common.grpc.pb.h"
#include "src/libruntime/invokeadaptor/report_record.h"
#include "src/libruntime/libruntime_config.h"
#include "src/libruntime/utils/serializer.h"
#include "src/libruntime/utils/utils.h"
#include "src/utility/id_generator.h"
#include "src/utility/logger/logger.h"
#include "src/utility/timer_worker.h"
namespace YR {
namespace Libruntime {
const float FLOAT_EQUAL_RANGE = 1e-6;
const std::string CONCURRENCY = "Concurrency";
const std::string CONCURRENT_NUM = "ConcurrentNum";
const std::string RELIABILITY_TYPE = "ReliabilityType";
const std::string LIFECYCLE = "lifecycle";
const std::string DETACHED = "detached";
const std::string ACQUIREPREFIX = "acquire#";
const std::string DEVICE_INFO = "device";
const std::string TENANT_ID = "tenantId";
const std::string DELEGATE_POD_LABELS = "DELEGATE_POD_LABELS";
const std::string DELEGATE_ENV_VAR = "DELEGATE_ENV_VAR";
const std::string YR_ROUTE = "YR_ROUTE";
extern const char *STORAGE_TYPE;
extern const char *CODE_PATH;
extern const char *WORKING_DIR;
extern const char *DELEGATE_DOWNLOAD;
const std::string NEED_ORDER = "need_order";
const std::string RECOVER_RETRY_TIMES = "RecoverRetryTimes";

struct InvokeSpec {
    InvokeSpec(const std::string &jobId, const FunctionMeta &functionMeta, const std::vector<DataObject> &returnObjs,
               std::vector<InvokeArg> invokeArgs, const libruntime::InvokeType invokeType, std::string traceId,
               std::string requestId, const std::string &instanceId, const InvokeOptions &opts);
    InvokeSpec() : requestInvoke(std::make_shared<InvokeMessageSpec>()) {}
    ~InvokeSpec() = default;
    std::string jobId;
    FunctionMeta functionMeta;
    std::vector<DataObject> returnIds;
    std::vector<InvokeArg> invokeArgs;
    libruntime::InvokeType invokeType;
    std::string traceId;
    std::string requestId;
    std::string instanceId = "";
    InvokeOptions opts;
    std::string poolLabel;
    std::string designatedInstanceID;
    std::string invokeInstanceId = "";
    std::string invokeLeaseId = "";
    bool reqShouldAbort = false;
    int64_t invokeSeqNo = 0;
    int64_t invokeUnfinishedSeqNo = 0;
    CreateRequest requestCreate;
    std::shared_ptr<InvokeMessageSpec> requestInvoke;
    uint8_t seq = 0;
    std::string instanceRoute = "";

    void ConsumeRetryTime(void);

    void IncrementSeq();

    std::string ConstructRequestID();

    template <typename Request>
    void IncrementRequestID(Request &request)
    {
        IncrementSeq();
        request.set_requestid(ConstructRequestID());
    }

    std::string GetInstanceId(std::shared_ptr<LibruntimeConfig> config);

    std::string GetNamedInstanceId();

    void InitDesignatedInstanceId(const LibruntimeConfig &config);

    bool IsStaleDuplicateNotify(uint8_t sequence);

    void BuildRequestPbOptions(InvokeOptions &opts, const LibruntimeConfig &config, CreateRequest &request);

    void BuildInstanceCreateRequest(const LibruntimeConfig &config);

    void BuildInstanceInvokeRequest(const LibruntimeConfig &config);

    std::string BuildCreateMetaData(const LibruntimeConfig &config);

    std::string BuildInvokeMetaData(const LibruntimeConfig &config);

    template <typename T>
    void BuildRequestPbArgs(const LibruntimeConfig &config, T &request, bool isCreate)
    {
        Arg *pbArg;
        if (functionMeta.apiType != libruntime::ApiType::Posix) {
            std::string metaData;
            if (isCreate) {
                metaData = BuildCreateMetaData(config);
            } else {
                metaData = BuildInvokeMetaData(config);
            }

            pbArg = request.add_args();
            pbArg->set_type(Arg_ArgType::Arg_ArgType_VALUE);
            pbArg->set_value(metaData);
        }
        for (unsigned int index = 0; index < this->invokeArgs.size(); ++index) {
            pbArg = request.add_args();
            auto arg = std::ref(const_cast<InvokeArg &>(this->invokeArgs[index]));
            if (arg.get().isRef) {
                pbArg->set_value(arg.get().objId.c_str(), arg.get().objId.size());
                pbArg->set_type(Arg_ArgType::Arg_ArgType_OBJECT_REF);
            } else if (arg.get().dataObj->buffer->IsString()) {
                auto strBuffer = std::dynamic_pointer_cast<StringNativeBuffer>(arg.get().dataObj->buffer);
                pbArg->set_value(strBuffer->StringData());
                pbArg->set_type(Arg_ArgType::Arg_ArgType_VALUE);
            } else {
                *pbArg->mutable_value() =
                    std::move(std::string(reinterpret_cast<const char *>(arg.get().dataObj->buffer->ImmutableData()),
                                          arg.get().dataObj->buffer->GetSize()));
                pbArg->set_type(Arg_ArgType::Arg_ArgType_VALUE);
            }
            for (auto &id : arg.get().nestedObjects) {
                pbArg->add_nested_refs(id);
            }
        }
    }

private:
    void BuildRequestPbScheduleOptions(InvokeOptions &opts, const LibruntimeConfig &config, CreateRequest &request);
    void BuildRequestPbCreateOptions(InvokeOptions &opts, const LibruntimeConfig &config, CreateRequest &request);
};

struct FaasAllocationInfo {
    std::string functionId;
    std::string funcSig;
    int tLeaseInterval;
    std::string schedulerInstanceID;
    std::string schedulerFunctionID;
    // ReportRecord record; // metrics, faas scheduler弹性依据
};

struct InstanceInfo {
    std::string instanceId = "" ABSL_GUARDED_BY(mtx);
    std::string leaseId = "" ABSL_GUARDED_BY(mtx);
    int idleTime = 0 ABSL_GUARDED_BY(mtx);
    int unfinishReqNum = 0 ABSL_GUARDED_BY(mtx);  // or avaliable
    bool available = true ABSL_GUARDED_BY(mtx);
    std::string traceId = "" ABSL_GUARDED_BY(mtx);
    FaasAllocationInfo faasInfo ABSL_GUARDED_BY(mtx);
    std::shared_ptr<ReportRecord> reporter ABSL_GUARDED_BY(mtx);
    std::string stateId = "" ABSL_GUARDED_BY(mtx);
    std::shared_ptr<YR::utility::Timer> scaleDownTimer ABSL_GUARDED_BY(mtx);
    int64_t claimTime = 0 ABSL_GUARDED_BY(mtx);
    mutable absl::Mutex mtx;
};

struct CreatingInsInfo {
    std::string instanceId ABSL_GUARDED_BY(mtx);
    int64_t startTime ABSL_GUARDED_BY(mtx);
    mutable absl::Mutex mtx;
    CreatingInsInfo(const std::string &id = "", int64_t time = 0) : instanceId(id), startTime(time) {}
};

struct RequestResourceInfo {
    std::unordered_map<std::string, std::shared_ptr<InstanceInfo>> instanceInfos ABSL_GUARDED_BY(mtx);
    std::unordered_map<std::string, std::shared_ptr<InstanceInfo>> avaliableInstanceInfos ABSL_GUARDED_BY(mtx);
    std::vector<std::shared_ptr<CreatingInsInfo>> creatingIns ABSL_GUARDED_BY(mtx);
    int createFailInstanceNum ABSL_GUARDED_BY(mtx);
    // The time to create an instance. when cancle a creating instance
    // the waiting time should not be less than the createTime.
    int createTime ABSL_GUARDED_BY(mtx);
    mutable absl::Mutex mtx;
};

struct RequestResource {
    FunctionMeta functionMeta;
    size_t concurrency;
    InvokeOptions opts;
    bool operator==(const RequestResource &r) const;
    void Print(void) const;
};

struct HashFn {
    std::size_t operator()(const RequestResource &r) const;
};

struct ConcurrencyGroup {
    std::string name;
    uint32_t maxConcurrency;
    std::vector<FunctionMeta> metas;
};
}  // namespace Libruntime
}  // namespace YR
