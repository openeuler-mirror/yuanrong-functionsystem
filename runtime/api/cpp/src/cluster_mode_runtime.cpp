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

// Implement GenericModeRuntime

#include <chrono>
#include <vector>
#include "stdio.h"

#include "api/cpp/include/yr/api/constant.h"
#include "api/cpp/include/yr/api/exception.h"
#include "api/cpp/include/yr/api/hetero_exception.h"
#include "api/cpp/include/yr/api/object_store.h"
#include "api/cpp/src/cluster_mode_runtime.h"
#include "api/cpp/src/code_manager.h"
#include "api/cpp/src/executor/executor_holder.h"
#include "api/cpp/src/hetero_future.h"
#include "api/cpp/src/read_only_buffer.h"
#include "api/cpp/src/state_loader.h"
#include "api/cpp/src/utils/utils.h"
#include "src/dto/data_object.h"
#include "src/dto/internal_wait_result.h"
#include "src/libruntime/err_type.h"
#include "src/libruntime/libruntime_manager.h"
#include "src/proto/libruntime.pb.h"
#include "src/utility/logger/logger.h"
#include "src/utility/string_utility.h"

namespace YR {
using YR::Libruntime::DataObject;

internal::FuncMeta convertToInternalFuncMeta(YR::Libruntime::FunctionMeta &libFuncMeta)
{
    internal::FuncMeta funcMeta;
    funcMeta.appName = libFuncMeta.appName;
    funcMeta.moduleName = libFuncMeta.moduleName;
    funcMeta.funcName = libFuncMeta.funcName;
    funcMeta.funcUrn = libFuncMeta.functionId;
    funcMeta.className = libFuncMeta.className;
    funcMeta.language = static_cast<internal::FunctionLanguage>(libFuncMeta.languageType);
    funcMeta.name = libFuncMeta.name;
    funcMeta.ns = libFuncMeta.ns;
    funcMeta.isAsync = libFuncMeta.isAsync;
    funcMeta.isGenerator = libFuncMeta.isGenerator;
    return funcMeta;
}

static libruntime::LanguageType ConvertLanguageType(const YR::internal::FunctionLanguage lang)
{
    if (lang == YR::internal::FunctionLanguage::FUNC_LANG_CPP) {
        return libruntime::LanguageType::Cpp;
    } else if (lang == YR::internal::FunctionLanguage::FUNC_LANG_PYTHON) {
        return libruntime::LanguageType::Python;
    } else if (lang == YR::internal::FunctionLanguage::FUNC_LANG_JAVA) {
        return libruntime::LanguageType::Java;
    }
    YRLOG_DEBUG("language not supported, lang: {}", static_cast<int>(lang));
    throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, "language not supported");
}

std::shared_ptr<YR::Libruntime::LabelOperator> GetLabelOperator(const std::string &operatorType)
{
    if (operatorType == LABEL_IN) {
        return std::make_shared<YR::Libruntime::LabelInOperator>();
    } else if (operatorType == LABEL_NOT_IN) {
        return std::make_shared<YR::Libruntime::LabelNotInOperator>();
    } else if (operatorType == LABEL_EXISTS) {
        return std::make_shared<YR::Libruntime::LabelExistsOperator>();
    } else if (operatorType == LABEL_DOES_NOT_EXIST) {
        return std::make_shared<YR::Libruntime::LabelDoesNotExistOperator>();
    }
    throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, "label type not supported");
}

std::shared_ptr<YR::Libruntime::Affinity> GetAffinity(const std::string &key)
{
    if (key == "ResourcePreferredAffinity") {
        return std::make_shared<YR::Libruntime::ResourcePreferredAffinity>();
    } else if (key == "ResourcePreferredAntiAffinity") {
        return std::make_shared<YR::Libruntime::ResourcePreferredAntiAffinity>();
    } else if (key == "ResourceRequiredAffinity") {
        return std::make_shared<YR::Libruntime::ResourceRequiredAffinity>();
    } else if (key == "ResourceRequiredAntiAffinity") {
        return std::make_shared<YR::Libruntime::ResourceRequiredAntiAffinity>();
    } else if (key == "InstancePreferredAffinity") {
        return std::make_shared<YR::Libruntime::InstancePreferredAffinity>();
    } else if (key == "InstancePreferredAntiAffinity") {
        return std::make_shared<YR::Libruntime::InstancePreferredAntiAffinity>();
    } else if (key == "InstanceRequiredAffinity") {
        return std::make_shared<YR::Libruntime::InstanceRequiredAffinity>();
    } else if (key == "InstanceRequiredAntiAffinity") {
        return std::make_shared<YR::Libruntime::InstanceRequiredAntiAffinity>();
    }
    throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, "affinity kind or type not supported");
}

std::list<std::shared_ptr<YR::Libruntime::LabelOperator>> BuildLibLabelOperators(std::list<LabelOperator> operators)
{
    std::list<std::shared_ptr<YR::Libruntime::LabelOperator>> libLabelOperators;
    for (auto &labelOperator : operators) {
        auto operatorType = labelOperator.GetOperatorType();
        auto operatorKey = labelOperator.GetKey();
        auto operatorValues = labelOperator.GetValues();
        std::shared_ptr<YR::Libruntime::LabelOperator> libLabelOperator = GetLabelOperator(operatorType);
        libLabelOperator->SetKey(operatorKey);
        libLabelOperator->SetValues(operatorValues);
        libLabelOperators.push_back(libLabelOperator);
    }
    return libLabelOperators;
}

std::list<std::shared_ptr<YR::Libruntime::Affinity>> BuildScheduleAffinities(const std::list<YR::Affinity> &affinities,
                                                                             bool requiredPriority,
                                                                             bool preferredPriority,
                                                                             bool preferredAntiOtherLabels)
{
    std::list<std::shared_ptr<YR::Libruntime::Affinity>> libAffinities;
    for (auto &affinity : affinities) {
        auto operators = affinity.GetLabelOperators();
        auto libLabelOperators = BuildLibLabelOperators(operators);
        auto affinityKind = affinity.GetAffinityKind();
        auto affinityType = affinity.GetAffinityType();
        std::string key = affinityKind + affinityType;
        std::shared_ptr<YR::Libruntime::Affinity> libAffinity = GetAffinity(key);
        libAffinity->SetLabelOperators(libLabelOperators);
        libAffinity->SetPreferredPriority(preferredPriority);
        libAffinity->SetRequiredPriority(requiredPriority);
        libAffinity->SetPreferredAntiOtherLabels(preferredAntiOtherLabels);
        libAffinities.push_back(libAffinity);
    }
    return libAffinities;
}

YR::Libruntime::InstanceRange BuildInstanceRange(const YR::InstanceRange &instanceRange)
{
    YR::Libruntime::InstanceRange range;
    YR::Libruntime::RangeOptions opts;
    range.max = instanceRange.max;
    range.min = instanceRange.min;
    range.step = instanceRange.step;
    range.sameLifecycle = instanceRange.sameLifecycle;
    opts.timeout = instanceRange.rangeOpts.timeout;
    range.rangeOpts = opts;
    return range;
}

YR::Libruntime::SetParam BuildSetParam(const YR::SetParam &setParam)
{
    YR::Libruntime::SetParam dsSetParam;
    dsSetParam.existence = static_cast<YR::Libruntime::ExistenceOpt>(setParam.existence);
    dsSetParam.ttlSecond = setParam.ttlSecond;
    dsSetParam.writeMode = static_cast<YR::Libruntime::WriteMode>(setParam.writeMode);
    dsSetParam.cacheType = YR::Libruntime::CacheType::MEMORY;
    return dsSetParam;
}

YR::Libruntime::SetParam BuildSetParamV2(const YR::SetParamV2 &setParam)
{
    YR::Libruntime::SetParam dsSetParam;
    dsSetParam.existence = static_cast<YR::Libruntime::ExistenceOpt>(setParam.existence);
    dsSetParam.ttlSecond = setParam.ttlSecond;
    dsSetParam.writeMode = static_cast<YR::Libruntime::WriteMode>(setParam.writeMode);
    dsSetParam.cacheType = static_cast<YR::Libruntime::CacheType>(setParam.cacheType);
    dsSetParam.extendParams = setParam.extendParams;
    return dsSetParam;
}

YR::Libruntime::MSetParam BuildMSetParam(const YR::MSetParam &mSetParam)
{
    YR::Libruntime::MSetParam dsMSetParam;
    dsMSetParam.existence = static_cast<YR::Libruntime::ExistenceOpt>(mSetParam.existence);
    dsMSetParam.ttlSecond = mSetParam.ttlSecond;
    dsMSetParam.writeMode = static_cast<YR::Libruntime::WriteMode>(mSetParam.writeMode);
    dsMSetParam.cacheType = static_cast<YR::Libruntime::CacheType>(mSetParam.cacheType);
    dsMSetParam.extendParams = mSetParam.extendParams;
    return dsMSetParam;
}

YR::Libruntime::GetParams BuildGetParam(const YR::GetParams &params)
{
    YR::Libruntime::GetParams dsParams;
    for (const auto &param : params.getParams) {
        YR::Libruntime::GetParam dsParam = {.offset = param.offset, .size = param.size};
        dsParams.getParams.emplace_back(dsParam);
    }
    return dsParams;
}

YR::Libruntime::CreateParam BuildCreateParam(const YR::CreateParam &createParam)
{
    YR::Libruntime::CreateParam dsCreateParam;
    dsCreateParam.writeMode = static_cast<YR::Libruntime::WriteMode>(createParam.writeMode);
    dsCreateParam.consistencyType = static_cast<YR::Libruntime::ConsistencyType>(createParam.consistencyType);
    dsCreateParam.cacheType = static_cast<YR::Libruntime::CacheType>(createParam.cacheType);
    return dsCreateParam;
}

YR::Libruntime::FunctionMeta BuildFunctionMeta(const internal::FuncMeta &funcMeta)
{
    YR::Libruntime::FunctionMeta libFunctionMeta;
    libFunctionMeta.appName = funcMeta.appName;
    libFunctionMeta.funcName = funcMeta.funcName;
    libFunctionMeta.moduleName = funcMeta.moduleName;
    libFunctionMeta.className = funcMeta.className;
    libFunctionMeta.languageType = ConvertLanguageType(funcMeta.language);
    if (!funcMeta.funcUrn.empty()) {
        libFunctionMeta.functionId = ConvertFunctionUrnToId(funcMeta.funcUrn);
    }
    if (funcMeta.name) {
        libFunctionMeta.name = funcMeta.name;
    }
    if (funcMeta.ns) {
        libFunctionMeta.ns = funcMeta.ns;
    }
    libFunctionMeta.apiType = libruntime::ApiType::Function;
    return libFunctionMeta;
}

std::vector<YR::Libruntime::InvokeArg> BuildInvokeArgs(std::vector<YR::internal::InvokeArg> &args)
{
    std::vector<YR::Libruntime::InvokeArg> libArgs;
    for (auto &arg : args) {
        YR::Libruntime::InvokeArg libArg;
        auto size = arg.buf.size();
        libArg.dataObj = std::make_shared<Libruntime::DataObject>(0, size);
        WriteDataObject(static_cast<void *>(arg.buf.data()), libArg.dataObj, size, {});
        libArg.isRef = arg.isRef;
        libArg.objId = arg.objId;
        libArg.nestedObjects = std::move(arg.nestedObjects);
        libArg.tenantId = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetTenantId();
        libArgs.emplace_back(std::move(libArg));
    }
    return libArgs;
}

YR::Libruntime::InvokeOptions BuildOptions(const YR::InvokeOptions &opts)
{
    YR::Libruntime::InvokeOptions libOpts;
    libOpts.affinity = opts.affinity;
    libOpts.retryTimes = opts.retryTimes;
    if (opts.retryChecker) {
        libOpts.retryChecker = [checker = opts.retryChecker](const Libruntime::ErrorInfo &err) -> bool {
            YR::Exception e(err.Code(), err.MCode(), err.Msg());
            return checker(e);
        };
    }
    libOpts.priority = opts.priority;
    libOpts.cpu = opts.cpu;
    libOpts.memory = opts.memory;
    libOpts.customResources = opts.customResources;
    libOpts.customExtensions = opts.customExtensions;
    libOpts.podLabels = opts.podLabels;
    libOpts.labels = opts.labels;
    libOpts.groupName = opts.groupName;
    libOpts.traceId = opts.traceId;
    if (opts.scheduleAffinities.size() > 0) {
        libOpts.scheduleAffinities = BuildScheduleAffinities(opts.scheduleAffinities, opts.requiredPriority,
                                                             opts.preferredPriority, opts.preferredAntiOtherLabels);
    }
    libOpts.needOrder = opts.needOrder;
    libOpts.instanceRange = BuildInstanceRange(opts.instanceRange);
    libOpts.recoverRetryTimes = opts.recoverRetryTimes;
    libOpts.envVars = opts.envVars;
    libOpts.timeout = opts.timeout;
    return libOpts;
}

// Counts are accumulated only after consecutive and limited retries.
bool IsRetryNeeded(Libruntime::RetryType retryType, int &limitedRetryTime)
{
    if (retryType == Libruntime::RetryType::UNLIMITED_RETRY) {
        limitedRetryTime = 0;
        return true;
    } else if (retryType == Libruntime::RetryType::LIMITED_RETRY) {
        limitedRetryTime++;
        return limitedRetryTime < LIMITED_RETRY_TIME;
    } else if (retryType == Libruntime::RetryType::NO_RETRY) {
        return false;
    }
    return false;
}

std::vector<YR::Libruntime::DeviceBlobList> BuildLibDeviceBlobList(const std::vector<DeviceBlobList> &devBlobList)
{
    std::vector<YR::Libruntime::DeviceBlobList> libDevBlobList;
    libDevBlobList.reserve(devBlobList.size());
    for (const auto &devBlob : devBlobList) {
        YR::Libruntime::DeviceBlobList libDevBlob;
        libDevBlob.deviceIdx = devBlob.deviceIdx;

        libDevBlob.blobs.reserve(devBlob.blobs.size());
        for (const auto &blob : devBlob.blobs) {
            YR::Libruntime::Blob libBlob;
            libBlob.pointer = blob.pointer;
            libBlob.size = blob.size;
            libDevBlob.blobs.emplace_back(std::move(libBlob));
        }
        libDevBlobList.push_back(libDevBlob);
    }
    return libDevBlobList;
}

AsyncResult ConvertAsyncResult(const YR::Libruntime::AsyncResult &libResult)
{
    AsyncResult result;
    ErrorInfo err;
    err.SetCodeAndMsg(static_cast<ErrorCode>(libResult.error.Code()), libResult.error.Msg());
    result.error = err;
    result.failedList = libResult.failedList;
    return result;
}

void ClusterModeRuntime::Init()
{
    YR::Libruntime::LibruntimeConfig libConfig;
    libConfig.inCluster = ConfigManager::Singleton().inCluster;
    ParseIpAddr(ConfigManager::Singleton().functionSystemAddr, libConfig.functionSystemIpAddr,
                libConfig.functionSystemPort);
    ParseIpAddr(ConfigManager::Singleton().grpcAddress, libConfig.functionSystemRtServerIpAddr,
                libConfig.functionSystemRtServerPort);
    ParseIpAddr(ConfigManager::Singleton().dataSystemAddr, libConfig.dataSystemIpAddr, libConfig.dataSystemPort);
    if (libConfig.functionSystemIpAddr.empty() || libConfig.functionSystemPort == 0 ||
        ((libConfig.dataSystemIpAddr.empty() || libConfig.dataSystemPort == 0) && libConfig.inCluster)) {
        std::stringstream ss;
        ss << "Invalid address of datasystem or function system, " << ConfigManager::Singleton().dataSystemAddr << " "
           << ConfigManager::Singleton().functionSystemAddr;
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                            ss.str());
    }
    libConfig.isDriver = ConfigManager::Singleton().isDriver;
    libConfig.jobId = ConfigManager::Singleton().jobId;
    libConfig.runtimeId = ConfigManager::Singleton().runtimeId;
    libConfig.enableServerMode = ConfigManager::Singleton().enableServerMode;
    // server in runtime does not support tls
    if (libConfig.inCluster && ConfigManager::Singleton().enableMTLS && !libConfig.enableServerMode) {
        std::stringstream ss;
        ss << "The in-cluster driver program does not support starting the TLS authentication server mode.";
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                            ss.str());
    }

    libConfig.selfLanguage = libruntime::LanguageType::Cpp;
    libConfig.functionIds[libruntime::LanguageType::Cpp] = ConfigManager::Singleton().functionId;
    libConfig.functionIds[libruntime::LanguageType::Python] = ConfigManager::Singleton().functionIdPython;
    libConfig.functionIds[libruntime::LanguageType::Java] = ConfigManager::Singleton().functionIdJava;

    libConfig.logLevel = ConfigManager::Singleton().logLevel;
    libConfig.logDir = ConfigManager::Singleton().logDir;
    libConfig.logFileSizeMax = ConfigManager::Singleton().maxLogFileSize;
    libConfig.logFileNumMax = ConfigManager::Singleton().maxLogFileNum;
    libConfig.logFlushInterval = ConfigManager::Singleton().logFlushInterval;
    libConfig.recycleTime = ConfigManager::Singleton().recycleTime;
    libConfig.maxTaskInstanceNum = ConfigManager::Singleton().maxTaskInstanceNum;
    libConfig.maxConcurrencyCreateNum = ConfigManager::Singleton().maxConcurrencyCreateNum;
    libConfig.enableMetrics = ConfigManager::Singleton().enableMetrics;
    libConfig.threadPoolSize = ConfigManager::Singleton().threadPoolSize;
    libConfig.localThreadPoolSize = ConfigManager::Singleton().localThreadPoolSize;
    libConfig.loadPaths = ConfigManager::Singleton().loadPaths;
    libConfig.tenantId = ConfigManager::Singleton().tenantId;

    libConfig.libruntimeOptions.functionExecuteCallback = internal::ExecuteFunction;
    libConfig.libruntimeOptions.loadFunctionCallback = internal::LoadFunctions;
    libConfig.libruntimeOptions.shutdownCallback = internal::ExecuteShutdownFunction;
    libConfig.libruntimeOptions.checkpointCallback = internal::Checkpoint;
    libConfig.libruntimeOptions.recoverCallback = internal::Recover;

    libConfig.enableMTLS = ConfigManager::Singleton().enableMTLS;
    if (ConfigManager::Singleton().enableMTLS) {
        libConfig.privateKeyPath = ConfigManager::Singleton().privateKeyPath;
        libConfig.certificateFilePath = ConfigManager::Singleton().certificateFilePath;
        libConfig.verifyFilePath = ConfigManager::Singleton().verifyFilePath;
    }
    libConfig.primaryKeyStoreFile = ConfigManager::Singleton().primaryKeyStoreFile;
    libConfig.standbyKeyStoreFile = ConfigManager::Singleton().standbyKeyStoreFile;
    libConfig.encryptEnable = ConfigManager::Singleton().enableDsEncrypt;
    if (ConfigManager::Singleton().enableDsEncrypt) {
        libConfig.runtimePublicKeyPath = ConfigManager::Singleton().runtimePublicKeyContextPath;
        libConfig.runtimePrivateKeyPath = ConfigManager::Singleton().runtimePrivateKeyContextPath;
        libConfig.dsPublicKeyPath = ConfigManager::Singleton().dsPublicKeyContextPath;
    }
    libConfig.serverName = ConfigManager::Singleton().serverName;
    libConfig.ns = ConfigManager::Singleton().ns;
    libConfig.customEnvs = ConfigManager::Singleton().customEnvs;
    libConfig.isLowReliabilityTask = ConfigManager::Singleton().isLowReliabilityTask;
    libConfig.attach = ConfigManager::Singleton().attach;
    ConfigManager::Singleton().ClearPasswd();
    auto errInfo = libConfig.Decrypt();
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
    errInfo = YR::Libruntime::LibruntimeManager::Instance().Init(libConfig);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
}

std::string ClusterModeRuntime::GetServerVersion()
{
    return YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetServerVersion();
}

std::string ClusterModeRuntime::CreateInstance(const internal::FuncMeta &funcMeta,
                                               std::vector<YR::internal::InvokeArg> &args, YR::InvokeOptions &opts)
{
    if (funcMeta.funcName.empty()) {
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                            FUNCTION_NOT_REGISTERED_ERROR_MSG);
    }
    auto functionMeta = BuildFunctionMeta(funcMeta);
    auto invokeArgs = BuildInvokeArgs(args);
    auto invokeOptions = BuildOptions(opts);
    YRLOG_DEBUG("create instance, function meta, name={}, language={}.", funcMeta.funcName,
                static_cast<int>(funcMeta.language));
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto [err, instanceId] = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->CreateInstance(
        functionMeta, invokeArgs, invokeOptions);
    if (err.Code() != Libruntime::ErrorCode::ERR_OK) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return instanceId;
}

std::string ClusterModeRuntime::InvokeInstance(const internal::FuncMeta &funcMeta, const std::string &instanceId,
                                               std::vector<YR::internal::InvokeArg> &args, const InvokeOptions &opts)
{
    YRLOG_DEBUG("invoke instance, function meta, name={}, language={}.", funcMeta.funcName,
                static_cast<int>(funcMeta.language));
    if (funcMeta.funcName.empty()) {
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                            FUNCTION_NOT_REGISTERED_ERROR_MSG);
    }
    std::vector<DataObject> returnObjs{{""}};
    auto libFunctionMeta = BuildFunctionMeta(funcMeta);
    auto libArgs = BuildInvokeArgs(args);
    auto libOpts = BuildOptions(opts);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->InvokeByInstanceId(
        libFunctionMeta, instanceId, libArgs, libOpts, returnObjs);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return returnObjs[0].id;
}

std::string ClusterModeRuntime::InvokeByName(const internal::FuncMeta &funcMeta,
                                             std::vector<YR::internal::InvokeArg> &args, const InvokeOptions &opts)
{
    YRLOG_DEBUG("start invoke function, name = {}, language={}.", funcMeta.funcName,
                static_cast<int>(funcMeta.language));
    if (funcMeta.funcName.empty()) {
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                            FUNCTION_NOT_REGISTERED_ERROR_MSG);
    }
    auto libFunctionMeta = BuildFunctionMeta(funcMeta);
    auto libArgs = BuildInvokeArgs(args);
    auto libOpts = BuildOptions(opts);
    std::vector<DataObject> returnObjs{{""}};
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->InvokeByFunctionName(
        libFunctionMeta, libArgs, libOpts, returnObjs);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return returnObjs[0].id;
}

void ClusterModeRuntime::TerminateInstance(const std::string &instanceId)
{
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Kill(instanceId);
    if (!errInfo.OK()) {
        YR::Exception exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
        throw exception;
    }
}

std::string ClusterModeRuntime::Put(std::shared_ptr<msgpack::sbuffer> data,
                                    const std::unordered_set<std::string> &nestedId)
{
    return Put(data, nestedId, {});
}

std::string ClusterModeRuntime::Put(std::shared_ptr<msgpack::sbuffer> data,
                                    const std::unordered_set<std::string> &nestedId, const CreateParam &createParam)
{
    auto param = BuildCreateParam(createParam);
    auto dataObj = std::make_shared<YR::Libruntime::DataObject>();
    std::vector<std::string> nestedIds(nestedId.begin(), nestedId.end());
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto [err, objId] = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->CreateDataObject(
        0, data->size(), dataObj, nestedIds, param);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_DEBUG("failed to Create DataObject {}", err.Msg());
        YR::Exception e2(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
        throw e2;
    }
    // copy data to DataObject data
    err = WriteDataObject(data->data(), dataObj, data->size(), nestedId);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_DEBUG("failed to WriteDataObject {}", err.Msg());
        YR::Exception e2(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
        throw e2;
    }
    return objId;
}

void ClusterModeRuntime::Put(const std::string &objId, std::shared_ptr<msgpack::sbuffer> data,
                             const std::unordered_set<std::string> &nestedId)
{
    auto dataObj = std::make_shared<YR::Libruntime::DataObject>();
    std::vector<std::string> nestedIds(nestedId.begin(), nestedId.end());
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->CreateDataObject(objId, 0, data->size(),
                                                                                               dataObj, nestedIds);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_DEBUG("failed to CreateDataObject {}", err.Msg());
        YR::Exception e2(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
        throw e2;
    }
    err = WriteDataObject(data->data(), dataObj, data->size(), nestedId);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_DEBUG("failed to WriteDataObject {}", err.Msg());
        YR::Exception e2(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
        throw e2;
    }
}

std::pair<internal::RetryInfo, std::vector<std::shared_ptr<Buffer>>> ClusterModeRuntime::Get(
    const std::vector<std::string> &ids, int timeoutMS, int &limitedRetryTime)
{
    internal::RetryInfo returnRetryInfo;
    returnRetryInfo.needRetry = true;
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto [retryInfo, dataObjects] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetDataObjectsWithoutWait(ids, timeoutMS);
    std::vector<std::shared_ptr<Buffer>> buffers;
    buffers.resize(dataObjects.size());
    std::vector<std::string> remainIds;
    for (size_t i = 0; i < dataObjects.size(); i++) {
        if (dataObjects[i] == nullptr) {
            remainIds.push_back(ids[i]);
            continue;
        }
        buffers[i] = std::make_shared<YR::ReadOnlyBuffer>(dataObjects[i]->data);
    }
    if (remainIds.size() > 0) {
        YRLOG_INFO("datasystem get partial objects; success objects: ({}/{}); retrying [{}, ...]",
                   ids.size() - remainIds.size(), ids.size(), remainIds[0]);
    }
    auto err = retryInfo.errorInfo;
    returnRetryInfo.errorInfo.SetErrorCode(static_cast<ErrorCode>(static_cast<int>(err.Code())));
    returnRetryInfo.errorInfo.SetModuleCode(static_cast<ModuleCode>(static_cast<int>(err.MCode())));
    returnRetryInfo.errorInfo.SetErrorMsg(err.Msg());
    if (!IsRetryNeeded(retryInfo.retryType, limitedRetryTime)) {
        returnRetryInfo.needRetry = false;
    }
    return std::pair<internal::RetryInfo, std::vector<std::shared_ptr<YR::Buffer>>>(returnRetryInfo, buffers);
}

YR::internal::WaitResult ClusterModeRuntime::Wait(const std::vector<std::string> &objs, std::size_t waitNum,
                                                  int timeout)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    std::shared_ptr<InternalWaitResult> internalWaitResult =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Wait(objs, waitNum, timeout);
    YR::internal::WaitResult waitResult;
    waitResult.readyIds = internalWaitResult->readyIds;
    waitResult.unreadyIds = internalWaitResult->unreadyIds;

    // To comply with the old version YR::Wait interface, if there is an error,
    // immediately convert it into YR::Exception and throw it.
    if (internalWaitResult->exceptionIds.size() > 0) {
        YR::Libruntime::ErrorInfo &err = internalWaitResult->exceptionIds.begin()->second;
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }

    return waitResult;
}

int64_t ClusterModeRuntime::WaitBeforeGet(const std::vector<std::string> &ids, int timeoutMs, bool allowPartial)
{
    auto [err, remainedTimeoutMs] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->WaitBeforeGet(ids, timeoutMs, allowPartial);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return remainedTimeoutMs;
}

std::vector<std::string> ClusterModeRuntime::GetInstances(const std::string &objId, int timeoutSec)
{
    if (timeoutSec < NO_TIMEOUT) {
        std::string msg =
            "invalid GetInstances timeout, timeout: " + std::to_string(timeoutSec) + ", please set the timeout >= -1.";
        throw YR::Exception(Libruntime::ErrorCode::ERR_PARAM_INVALID, Libruntime::ModuleCode::RUNTIME, msg);
    }
    auto [instanceIds, err] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetInstances(objId, timeoutSec);
    if (!err.OK()) {
        throw YR::Exception(err.Code(), err.MCode(), err.Msg());
    }
    return instanceIds;
}

std::string ClusterModeRuntime::GenerateGroupName()
{
    return YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GenerateGroupName();
}

void ClusterModeRuntime::IncreGlobalReference(const std::vector<std::string> &objids)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    // Here, LibRuntime return YR::Libruntime::ErrorInfo, should catch and cast type to YR::Exception.
    YR::Libruntime::ErrorInfo err =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->IncreaseReference(objids);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YR::Exception e2(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
        throw e2;
    }
}

void ClusterModeRuntime::DecreGlobalReference(const std::vector<std::string> &objids)
{
    if (YR::Libruntime::LibruntimeManager::Instance().IsInitialized()) {
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->DecreaseReference(objids);
    }
}

void ClusterModeRuntime::KVWrite(const std::string &key, const char *value, YR::SetParam setParam)
{
    auto dsSetParam = BuildSetParam(setParam);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(setParam.traceId);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    auto nativeBuffer = std::make_shared<YR::Libruntime::NativeBuffer>(static_cast<void *>(const_cast<char *>(value)),
                                                                       std::strlen(value));

    err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVWrite(key, nativeBuffer, dsSetParam);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVWrite err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

void ClusterModeRuntime::KVWrite(const std::string &key, std::shared_ptr<msgpack::sbuffer> value, YR::SetParam setParam)
{
    auto dsSetParam = BuildSetParam(setParam);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(setParam.traceId);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVWrite(
        key, std::make_shared<YR::Libruntime::MsgpackBuffer>(value), dsSetParam);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVWrite err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

void ClusterModeRuntime::KVWrite(const std::string &key, std::shared_ptr<msgpack::sbuffer> value,
                                 YR::SetParamV2 setParam)
{
    auto dsSetParam = BuildSetParamV2(setParam);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(setParam.traceId);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVWrite(
        key, std::make_shared<YR::Libruntime::MsgpackBuffer>(value), dsSetParam);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVWrite err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

void ClusterModeRuntime::KVMSetTx(const std::vector<std::string> &keys,
                                  const std::vector<std::shared_ptr<msgpack::sbuffer>> &vals, ExistenceOpt existence)
{
    MSetParam mSetParam;
    mSetParam.existence = existence;
    KVMSetTx(keys, vals, mSetParam);
}

void ClusterModeRuntime::KVMSetTx(const std::vector<std::string> &keys,
                                  const std::vector<std::shared_ptr<msgpack::sbuffer>> &vals,
                                  const MSetParam &mSetParam)
{
    auto dsMSetParam = BuildMSetParam(mSetParam);
    std::vector<std::shared_ptr<YR::Libruntime::Buffer>> buffers;
    buffers.resize(vals.size());
    for (size_t i = 0; i < vals.size(); i++) {
        buffers[i] = std::make_shared<YR::Libruntime::MsgpackBuffer>(vals[i]);
    }
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    YR::Libruntime::ErrorInfo err =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVMSetTx(keys, buffers, dsMSetParam);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVMSetTx err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

std::shared_ptr<Buffer> ClusterModeRuntime::KVRead(const std::string &key, int timeoutMs)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    YR::Libruntime::SingleReadResult result =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVRead(key, timeoutMs);
    YR::Libruntime::ErrorInfo &err = result.second;
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVRead err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return std::make_shared<YR::ReadOnlyBuffer>(result.first);
}

std::vector<std::shared_ptr<Buffer>> ClusterModeRuntime::KVRead(const std::vector<std::string> &keys, int timeoutMs,
                                                                bool allowPartial)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    YR::Libruntime::MultipleReadResult result =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVRead(keys, timeoutMs, allowPartial);
    YR::Libruntime::ErrorInfo &err = result.second;
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVRead err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    std::vector<std::shared_ptr<Buffer>> buffers;
    buffers.resize(result.first.size());
    for (size_t i = 0; i < result.first.size(); i++) {
        if (result.first[i] == nullptr) {
            continue;
        }
        buffers[i] = std::make_shared<YR::ReadOnlyBuffer>(result.first[i]);
    }
    return buffers;
}

std::vector<std::shared_ptr<Buffer>> ClusterModeRuntime::KVGetWithParam(const std::vector<std::string> &keys,
                                                                        const YR::GetParams &params, int timeoutMs)
{
    auto dsParams = BuildGetParam(params);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto res = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(params.traceId);
    if (res.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("Set trace id err: Code:{}, MCode:{}, Msg:{}", res.Code(), res.MCode(), res.Msg());
        throw YR::Exception(static_cast<int>(res.Code()), static_cast<int>(res.MCode()), res.Msg());
    }
    YR::Libruntime::MultipleReadResult result =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVGetWithParam(keys, dsParams, timeoutMs);
    YR::Libruntime::ErrorInfo &err = result.second;
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVGetWithParam err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    std::vector<std::shared_ptr<Buffer>> buffers;
    buffers.resize(result.first.size());
    for (size_t i = 0; i < result.first.size(); i++) {
        if (result.first[i] != nullptr) {
            buffers[i] = std::make_shared<YR::ReadOnlyBuffer>(result.first[i]);
        }
    }
    return buffers;
}

void ClusterModeRuntime::KVDel(const std::string &key, const DelParam &delParam)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(delParam.traceId);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVDel(key);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVDel err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

std::vector<std::string> ClusterModeRuntime::KVDel(const std::vector<std::string> &keys, const DelParam &delParam)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTenantIdWithPriority();
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SetTraceId(delParam.traceId);
    if (!err.OK()) {
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    YR::Libruntime::MultipleDelResult result =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->KVDel(keys);
    err = result.second;
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YRLOG_ERROR("KVDel err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
    return result.first;
}

std::string ClusterModeRuntime::GetRealInstanceId(const std::string &objectId)
{
    return YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetRealInstanceId(objectId);
}

void ClusterModeRuntime::SaveRealInstanceId(const std::string &objectId, const std::string &instanceId,
                                            const InvokeOptions &opts)
{
    YR::Libruntime::InstanceOptions instOpts;
    instOpts.needOrder = opts.needOrder;
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SaveRealInstanceId(objectId, instanceId, instOpts);
}

std::string ClusterModeRuntime::GetGroupInstanceIds(const std::string &objectId)
{
    return YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetGroupInstanceIds(objectId, NO_TIMEOUT);
}

void ClusterModeRuntime::SaveGroupInstanceIds(const std::string &objectId, const std::string &groupInsIds,
                                              const InvokeOptions &opts)
{
    YR::Libruntime::InstanceOptions instOpts;
    instOpts.needOrder = opts.needOrder;
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SaveGroupInstanceIds(objectId, groupInsIds,
                                                                                        instOpts);
}

void ClusterModeRuntime::Cancel(const std::vector<std::string> &objs, bool isForce, bool isRecursive)
{
    YR::Libruntime::ErrorInfo err =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Cancel(objs, isForce, isRecursive);
    if (!err.OK()) {
        YRLOG_DEBUG("Cancel err: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
        throw YR::Exception(static_cast<int>(err.Code()), static_cast<int>(err.MCode()), err.Msg());
    }
}

void ClusterModeRuntime::Exit(void)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Exit();
}

void ClusterModeRuntime::StopRuntime(void)
{
    YR::Libruntime::LibruntimeManager::Instance().Finalize();
}

bool ClusterModeRuntime::IsOnCloud()
{
    return !ConfigManager::Singleton().isDriver;
}

void ClusterModeRuntime::GroupCreate(const std::string &name, GroupOptions &opts)
{
    YR::Libruntime::GroupOpts libOpts;
    if (opts.timeout != -1 && opts.timeout < 0) {
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID,
                            "The value of timeout should be -1 or greater than 0");
    }
    libOpts.timeout = opts.timeout;
    libOpts.groupName = name;
    libOpts.sameLifecycle = opts.sameLifecycle;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GroupCreate(name, libOpts);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
}

void ClusterModeRuntime::GroupTerminate(const std::string &name)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GroupTerminate(name);
}

void ClusterModeRuntime::GroupWait(const std::string &name)
{
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GroupWait(name);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
}

void ClusterModeRuntime::SaveState(const int &timeout)
{
    std::shared_ptr<YR::Libruntime::Buffer> data;
    auto dumpErr = internal::Checkpoint("", data);
    if (!dumpErr.OK()) {
        throw YR::Exception(static_cast<int>(dumpErr.Code()), static_cast<int>(dumpErr.MCode()), dumpErr.Msg());
    }
    int timeoutMS = timeout != NO_TIMEOUT ? timeout * S_TO_MS : NO_TIMEOUT;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SaveState(data, timeoutMS);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
}

void ClusterModeRuntime::LoadState(const int &timeout)
{
    std::shared_ptr<YR::Libruntime::Buffer> data;
    int timeoutMS = timeout != NO_TIMEOUT ? timeout * S_TO_MS : NO_TIMEOUT;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->LoadState(data, timeoutMS);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
    auto loadErr = internal::Recover(data);
    if (!loadErr.OK()) {
        throw YR::Exception(static_cast<int>(loadErr.Code()), static_cast<int>(loadErr.MCode()), loadErr.Msg());
    }
}

void ClusterModeRuntime::Delete(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds)
{
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Delete(objectIds, failedObjectIds);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg(),
                                  failedObjectIds);
    }
}

void ClusterModeRuntime::LocalDelete(const std::vector<std::string> &objectIds,
                                     std::vector<std::string> &failedObjectIds)
{
    auto errInfo =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->LocalDelete(objectIds, failedObjectIds);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg(),
                                  failedObjectIds);
    }
}

void ClusterModeRuntime::DevSubscribe(const std::vector<std::string> &keys,
                                      const std::vector<DeviceBlobList> &blob2dList,
                                      std::vector<std::shared_ptr<YR::Future>> &futureVec)
{
    auto libDevBlobList = BuildLibDeviceBlobList(blob2dList);
    std::vector<std::shared_ptr<YR::Libruntime::HeteroFuture>> libHeteroFutureVec;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->DevSubscribe(keys, libDevBlobList,
                                                                                               libHeteroFutureVec);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
    for (auto libFuture : libHeteroFutureVec) {
        futureVec.push_back(std::make_shared<YR::HeteroFuture>(libFuture));
    }
}

void ClusterModeRuntime::DevPublish(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                    std::vector<std::shared_ptr<YR::Future>> &futureVec)
{
    auto libDevBlobList = BuildLibDeviceBlobList(blob2dList);
    std::vector<std::shared_ptr<YR::Libruntime::HeteroFuture>> libHeteroFutureVec;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->DevPublish(keys, libDevBlobList,
                                                                                             libHeteroFutureVec);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
    for (auto libFuture : libHeteroFutureVec) {
        futureVec.push_back(std::make_shared<YR::HeteroFuture>(libFuture));
    }
}

void ClusterModeRuntime::DevMSet(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                 std::vector<std::string> &failedKeys)
{
    auto libDevBlobList = BuildLibDeviceBlobList(blob2dList);
    auto errInfo =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->DevMSet(keys, libDevBlobList, failedKeys);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg(),
                                  failedKeys);
    }
}

void ClusterModeRuntime::DevMGet(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                 std::vector<std::string> &failedKeys, int32_t timeoutSec)
{
    auto libDevBlobList = BuildLibDeviceBlobList(blob2dList);
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->DevMGet(keys, libDevBlobList,
                                                                                          failedKeys, timeoutSec);
    if (!errInfo.OK()) {
        throw YR::HeteroException(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg(),
                                  failedKeys);
    }
}

internal::FuncMeta ClusterModeRuntime::GetInstance(const std::string &name, const std::string &nameSpace,
                                                   int timeoutSec)
{
    auto [funcMeta, errInfo] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetInstance(name, nameSpace, timeoutSec);
    if (!errInfo.OK()) {
        throw YR::Exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
    }
    return convertToInternalFuncMeta(funcMeta);
}

std::string ClusterModeRuntime::GetInstanceRoute(const std::string &objectId)
{
    return YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->GetInstanceRoute(objectId);
}

void ClusterModeRuntime::SaveInstanceRoute(const std::string &objectId, const std::string &instanceRoute)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->SaveInstanceRoute(objectId, instanceRoute);
}

void ClusterModeRuntime::TerminateInstanceSync(const std::string &instanceId)
{
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->Kill(
        instanceId, libruntime::Signal::killInstanceSync);
    if (!errInfo.OK()) {
        YR::Exception exception(static_cast<int>(errInfo.Code()), static_cast<int>(errInfo.MCode()), errInfo.Msg());
        throw exception;
    }
}
}  // namespace YR
