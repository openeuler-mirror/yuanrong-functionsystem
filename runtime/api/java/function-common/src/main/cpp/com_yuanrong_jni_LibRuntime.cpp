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

#include "com_yuanrong_jni_LibRuntime.h"
#include <jni.h>

#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <msgpack.hpp>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "jni_init.h"
#include "jni_types.h"

#include "src/dto/internal_wait_result.h"
#include "src/dto/invoke_arg.h"
#include "src/dto/invoke_options.h"
#include "src/dto/status.h"
#include "src/libruntime/auto_init.h"
#include "src/libruntime/err_type.h"
#include "src/libruntime/invokeadaptor/request_manager.h"
#include "src/libruntime/libruntime_config.h"
#include "src/libruntime/libruntime_manager.h"
#include "src/libruntime/libruntime_options.h"

#include "src/proto/libruntime.pb.h"

/**
 * Java provide:
 *   - CodeLoader: load and find class/instance/methods
 *      - Load(String[])
 *   - CodeExecutor: execute code and return the return value according to function metadata
 *      - Execute: excute functions, may create also
 */
thread_local JNIEnv *local_env = nullptr;
extern JavaVM *jvm;

JNIEnv *GetJNIEnv()
{
    JNIEnv *env = local_env;
    if (!env) {
        // Attach the native thread to JVM.
        auto status = jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void **>(&env), nullptr);
        if (status != JNI_OK) {
            std::cerr << "Failed to get JNIEnv. Return code: " << status << std::endl;
        }
        local_env = env;
    }
    RETURN_IF_NULL(env, nullptr);
    return env;
}

#ifdef __cplusplus
extern "C" {
#endif

#define CHECK_ERROR_AND_THROW(env, err, returnValue, msg)                              \
    {                                                                                  \
        if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {                         \
            YR::jni::JNILibruntimeException::Throw(env, err.Code(), err.MCode(), msg); \
            return returnValue;                                                        \
        }                                                                              \
    }

static jobject get_runtime_context_callback(JNIEnv *env, jclass c)
{
    jmethodID callbackMethodID = env->GetStaticMethodID(c, "GetRuntimeContext", "()Ljava/lang/String;");
    jobject result = env->CallStaticObjectMethod(c, callbackMethodID);
    return result;
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_Init(JNIEnv *env, jclass c, jobject jconfig)
{
    // definition of functionExecutaionCallback
    YR::Libruntime::LibruntimeOptions::FunctionExecuteCallback functionExecutaionCb =
        [](const YR::Libruntime::FunctionMeta &funcMeta, const libruntime::InvokeType &invokeType,
           const std::vector<std::shared_ptr<YR::Libruntime::DataObject>> &rawArgs,
           std::vector<std::shared_ptr<YR::Libruntime::DataObject>> &returnValues) -> YR::Libruntime::ErrorInfo {
        JNIEnv *env = GetJNIEnv();
        RETURN_IF_NULL(
            env, YR::Libruntime::ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, "Failed to get JNI env"));
        return YR::jni::JNICodeExecutor::Execute(env, funcMeta, invokeType, rawArgs, returnValues);
    };

    // definition of functionLoadCallback
    YR::Libruntime::LibruntimeOptions::LoadFunctionCallback functionLoadCb =
        [](const std::vector<std::string> &codePaths) -> YR::Libruntime::ErrorInfo {
        JNIEnv *env = GetJNIEnv();
        RETURN_IF_NULL(
            env, YR::Libruntime::ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, "Failed to get JNI env"));
        return YR::jni::JNICodeLoader::Load(env, codePaths);
    };

    // definition of checkpointCallback
    YR::Libruntime::LibruntimeOptions::CheckpointCallback checkpointCb =
        [](const std::string &checkpointId,
           std::shared_ptr<YR::Libruntime::Buffer> &data) -> YR::Libruntime::ErrorInfo {
        JNIEnv *env = GetJNIEnv();
        RETURN_IF_NULL(
            env, YR::Libruntime::ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, "Failed to get JNI env"));
        return YR::jni::JNICodeExecutor::DumpInstance(env, checkpointId, data);
    };

    // definition of recoverCallback
    YR::Libruntime::LibruntimeOptions::RecoverCallback recoverCb =
        [](std::shared_ptr<YR::Libruntime::Buffer> data) -> YR::Libruntime::ErrorInfo {
        JNIEnv *env = GetJNIEnv();
        RETURN_IF_NULL(
            env, YR::Libruntime::ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, "Failed to get JNI env"));
        return YR::jni::JNICodeExecutor::LoadInstance(env, data);
    };

    // definition of shutdownCallback
    YR::Libruntime::LibruntimeOptions::ShutdownCallback functionShutdownCb =
        [](uint64_t gracePeriodSeconds) -> YR::Libruntime::ErrorInfo {
        JNIEnv *env = GetJNIEnv();
        RETURN_IF_NULL(
            env, YR::Libruntime::ErrorInfo(YR::Libruntime::ErrorCode::ERR_INNER_SYSTEM_ERROR, "Failed to get JNI env"));
        return YR::jni::JNICodeExecutor::Shutdown(env, gracePeriodSeconds);
    };

    auto config = YR::jni::JNILibRuntimeConfig::FromJava(env, jconfig);
    config.libruntimeOptions.functionExecuteCallback = functionExecutaionCb;
    config.libruntimeOptions.loadFunctionCallback = functionLoadCb;
    config.libruntimeOptions.checkpointCallback = checkpointCb;
    config.libruntimeOptions.recoverCallback = recoverCb;
    config.libruntimeOptions.shutdownCallback = functionShutdownCb;
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto err = YR::Libruntime::LibruntimeManager::Instance().Init(config, rtCtx);
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert jerr when Libruntime_Init, get null");
        return nullptr;
    }
    return jerr;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    CreateInstance
 * Signature:
 * (Lcom/yuanrong/libruntime/generated/Libruntime$FunctionMeta;Ljava/util/List;Lcom/yuanrong/InvokeOptions;)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_CreateInstance(JNIEnv *env, jclass c,
                                                                                 jobject functionMeta, jobject args,
                                                                                 jobject opt)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto funcMeta = YR::jni::JNIFunctionMeta::FromJava(env, functionMeta);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(env, nullptr,
                                                           "exception occurred when convert funcMeta from java to cc");
    auto invokeArgs = YR::jni::JNIInvokeArg::FromJavaList(
        env, args, YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GetTenantId());
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert invokeArgs from java to cc");
    auto invokeOptions = YR::jni::JNIInvokeOptions::FromJava(env, opt);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert invokeOptions from java to cc");

    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    auto [err, objectID] = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->CreateInstance(
        funcMeta, invokeArgs, invokeOptions);

    if (!err.OK()) {
        YRLOG_WARN("failed to CreateInstance, err({}), msg({})", err.Code(), err.Msg());
        YR::jni::JNILibruntimeException::Throw(
            env, err.Code(), err.MCode(),
            "failed to CreateInstance, err " + std::to_string(err.Code()) + ", msg: " + err.Msg());
        return nullptr;
    }

    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YRLOG_WARN("failed to convert error info from cc to java, get null");
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert error info from cc, get null");
        return nullptr;
    }

    jstring jobjectID = YR::jni::JNIString::FromCc(env, objectID);
    if (jobjectID == nullptr) {
        YRLOG_WARN("failed to convert objectID from cc to java, get null");
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert objectID from cc, get null");
        return nullptr;
    }

    return YR::jni::JNIPair::CreateJPair(env, jerr, jobjectID);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    InvokeInstance
 * Signature:
 * (Lcom/yuanrong/libruntime/generated/Libruntime$FunctionMeta;Ljava/lang/String;Ljava/util/List;Lcom/yuanrong/InvokeOptions;)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_InvokeInstance(JNIEnv *env, jclass c,
                                                                                 jobject functionMeta,
                                                                                 jstring instanceId, jobject args,
                                                                                 jobject opt)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto funcMeta = YR::jni::JNIFunctionMeta::FromJava(env, functionMeta);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(env, nullptr,
                                                           "exception occurred when convert funcMeta from java to cc");
    auto instanceIdStr = YR::jni::JNIString::FromJava(env, instanceId);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert instanceIdStr from java to cc");
    auto invokeArgs = YR::jni::JNIInvokeArg::FromJavaList(
        env, args, YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GetTenantId());
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert invokeArgs from java to cc");
    auto invokeOptions = YR::jni::JNIInvokeOptions::FromJava(env, opt);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert invokeOptions from java to cc");
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when convert returnDataObjs from java to cc");
    YR::Libruntime::ErrorInfo err;
    std::vector<YR::Libruntime::DataObject> returnDataObjs{{""}};
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    if (instanceIdStr.size() > 0) {
        err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->InvokeByInstanceId(
            funcMeta, instanceIdStr, invokeArgs, invokeOptions, returnDataObjs);
    } else {
        err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->InvokeByFunctionName(
            funcMeta, invokeArgs, invokeOptions, returnDataObjs);
    }
    if (!err.OK()) {
        YR::jni::JNILibruntimeException::Throw(
            env, err.Code(), err.MCode(),
            "failed to invokeByInstanceID, err " + std::to_string(err.Code()) + ", msg: " + err.Msg());
        return nullptr;
    }
    std::string returnDataObj = returnDataObjs[0].id;
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert ErrorInfo when invokeByInstanceID, get null");
        return nullptr;
    }
    jstring jreturnDataObjId = YR::jni::JNIString::FromCc(env, returnDataObj);
    if (jreturnDataObjId == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(
            env, "failed to convert ReturnDataObjId when invokeByInstanceID, get null");
        return nullptr;
    }
    return YR::jni::JNIPair::CreateJPair(env, jerr, jreturnDataObjId);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_Put(JNIEnv *env, jclass c, jbyteArray byteArray,
                                                                      jobject objectIds)
{
    auto nestedObjectIds = YR::jni::JNIString::FromJArrayToUnorderedSet(env, objectIds);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    std::shared_ptr<YR::Libruntime::DataObject> dataObj;
    auto errInfo = YR::jni::JNIDataObject::WriteDataObject(env, dataObj, byteArray);
    if (errInfo.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YR::jni::JNILibruntimeException::Throw(
            env, errInfo.Code(), errInfo.MCode(),
            "put finished, return code is " + std::to_string(errInfo.Code()) + ", msg: " + errInfo.Msg());
        return nullptr;
    }
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    auto [err, objId] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Put(dataObj, nestedObjectIds);
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    jstring jobjId = YR::jni::JNIString::FromCc(env, objId);

    return YR::jni::JNIPair::CreateJPair(env, jerr, jobjId);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_PutWithParam(JNIEnv *env, jclass c,
                                                                               jbyteArray byteArray, jobject objectIds,
                                                                               jobject createParam)
{
    auto nestedObjectIds = YR::jni::JNIString::FromJArrayToUnorderedSet(env, objectIds);
    auto ccreateParam = YR::jni::JNICreateParam::FromJava(env, createParam);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    std::shared_ptr<YR::Libruntime::DataObject> dataObj;
    auto errInfo = YR::jni::JNIDataObject::WriteDataObject(env, dataObj, byteArray);
    if (errInfo.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YR::jni::JNILibruntimeException::Throw(
            env, errInfo.Code(), errInfo.MCode(),
            "put finished, return code is " + std::to_string(errInfo.Code()) + ", msg: " + errInfo.Msg());
        return nullptr;
    }
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    auto [err, objId] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Put(dataObj, nestedObjectIds, ccreateParam);
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    jstring jobjId = YR::jni::JNIString::FromCc(env, objId);

    return YR::jni::JNIPair::CreateJPair(env, jerr, jobjId);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    Get
 * Signature: (Ljava/util/List;IZ)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_Get(JNIEnv *env, jclass c, jobject listOfIds,
                                                                      jint timeoutMs, jboolean allowPartial)
{
    auto objIds = YR::jni::JNIList::FromJava<std::string>(env, listOfIds, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "LibRuntime_Get called, but exception occurred when convert args from java to cc");
    int timeoutMsInt = static_cast<int>(timeoutMs);
    bool allowPartialBool = static_cast<bool>(allowPartial);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // A special constraint: call wait first before call get
    auto [err, res] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Get(objIds, timeoutMsInt, allowPartialBool);
    if (err.Code() != YR::Libruntime::ErrorCode::ERR_OK) {
        YR::jni::JNILibruntimeException::Throw(
            env, err.Code(), err.MCode(),
            "get finished, return code is " + std::to_string(err.Code()) + ", msg: " + err.Msg());
        return nullptr;
    }

    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert jerr when Libruntime_Get, get null");
        return nullptr;
    }

    jobject listResult = YR::jni::JNIDataObject::FromCcPtrVectorToList(env, res);
    if (listResult == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert listResult when Libruntime_Get, get null");
        return nullptr;
    }

    return YR::jni::JNIPair::CreateJPair(env, jerr, listResult);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_Wait(JNIEnv *env, jclass c, jobject objList,
                                                                       jint waitNum, jint timeoutSec)
{
    auto objIds = YR::jni::JNIList::FromJava<std::string>(env, objList, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "LibRuntime_Wait called, but exception occurred when convert args from java to cc");
    int timeoutSecInt = static_cast<int>(timeoutSec);
    int waitNumInt = static_cast<int>(waitNum);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    auto internalWaitResult =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Wait(objIds, waitNumInt, timeoutSecInt);
    jobject res = YR::jni::JNIInternalWaitResult::FromCc(env, internalWaitResult);
    if (res == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "get null when transform wait result from cpp to java");
    }
    CHECK_JAVA_EXCEPTION_AND_THROW_NEW_AND_RETURN_IF_OCCUR(
        env, nullptr, "exception occurred when waiting for internalWaitResult from cc to java");
    return res;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    DecreaseReference
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_DecreaseReference(JNIEnv *env, jclass c, jobject objList)
{
    auto objIds = YR::jni::JNIList::FromJava<std::string>(env, objList, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto libRuntime = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx);
    if (libRuntime) {
        libRuntime->SetTenantIdWithPriority();
        libRuntime->DecreaseReference(objIds);
    }
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    ReceiveRequestLoop
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_ReceiveRequestLoop(JNIEnv *env, jclass c)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().ReceiveRequestLoop(rtCtx);
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    FinalizeWithCtx
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_FinalizeWithCtx(JNIEnv *env, jclass c,
                                                                               jstring runtimeCtx)
{
    auto rtCtx = YR::jni::JNIString::FromJava(env, runtimeCtx);
    YR::Libruntime::LibruntimeManager::Instance().Finalize(rtCtx);
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    Finalize
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_Finalize(JNIEnv *env, jclass c)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().Finalize(rtCtx);
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    Exit
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_Exit(JNIEnv *env, jclass c)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Exit();
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    AutoInitYR
 * Signature: (Ljava/lang/YRAutoInitInfo;)Ljava/lang/YRAutoInitInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_AutoInitYR(JNIEnv *env, jclass c, jobject obj)
{
    auto pInfo = YR::jni::JNIYRAutoInitInfo::FromJava(env, obj);
    YR::Libruntime::ClusterAccessInfo info = YR::Libruntime::AutoGetClusterAccessInfo(pInfo);
    return YR::jni::JNIYRAutoInitInfo::FromCc(env, info);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_Kill(JNIEnv *env, jclass c, jstring instanceID)
{
    auto instID = YR::jni::JNIString::FromJava(env, instanceID);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Kill(instID);
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert jerr when Libruntime_Kill, get null");
        return nullptr;
    }
    return jerr;
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    IsInitialized
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_yuanrong_jni_LibRuntime_IsInitialized(JNIEnv *env, jclass c)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    return static_cast<jboolean>(YR::Libruntime::LibruntimeManager::Instance().IsInitialized(rtCtx));
}

/*
 * Class:     com_yuanrong_jni_NativeLibRuntime
 * Method:    setRuntimeContext
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_setRuntimeContext(JNIEnv *env, jclass c, jstring jobID)
{
    std::string cjobID = YR::jni::JNIString::FromJava(env, jobID);
    YR::Libruntime::LibruntimeManager::Instance().SetRuntimeContext(cjobID);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    GetRealInstanceId
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_yuanrong_jni_LibRuntime_GetRealInstanceId(JNIEnv *env, jclass c,
                                                                                    jstring objectID)
{
    std::string cobjectID = YR::jni::JNIString::FromJava(env, objectID);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto instanceID = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GetRealInstanceId(cobjectID);
    jstring jinstanceID = YR::jni::JNIString::FromCc(env, instanceID);
    ASSERT_NOT_NULL(jinstanceID);

    return jinstanceID;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    SaveRealInstanceId
 * Signature: (Ljava/lang/String;Ljava/lang/String;Lcom/yuanrong/InvokeOptions;)V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_SaveRealInstanceId(JNIEnv *env, jclass c,
                                                                                  jstring objectID, jstring instanceID,
                                                                                  jobject opt)
{
    std::string cobjectID = YR::jni::JNIString::FromJava(env, objectID);
    std::string cinstanceID = YR::jni::JNIString::FromJava(env, instanceID);
    auto opts = YR::jni::JNIInvokeOptions::FromJava(env, opt);
    YR::Libruntime::InstanceOptions instOpts;
    instOpts.needOrder = opts.needOrder;
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SaveRealInstanceId(cobjectID, cinstanceID,
                                                                                           instOpts);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVWrite
 * Signature: (Ljava/lang/String;[BLcom/yuanrong/Setparam;)Lcom/yuanrong/errorcode/ErrorInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVWrite(JNIEnv *env, jclass c, jstring key,
                                                                          jbyteArray value, jobject setParam)
{
    // parameters from java to cpp
    auto ckey = YR::jni::JNIString::FromJava(env, key);
    auto cvalue = YR::jni::JNIByteBuffer::FromJava(env, value);
    auto csetParam = YR::jni::JNISetParam::FromJava(env, setParam);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java
    auto cerrorInfo =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVWrite(ckey, cvalue, csetParam);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return jerrorInfo;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVMSetTx
 * Signature:
 * (Ljava/util/List;Ljava/util/List;Lcom/yuanrong/MSetparam;)Lcom/yuanrong/errorcode/ErrorInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVMSetTx(JNIEnv *env, jclass c, jobject keys,
                                                                           jobject values, jobject mSetParam)
{
    // parameters from java to cpp
    auto ckeys = YR::jni::JNIList::FromJava<std::string>(env, keys, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto cvalues = YR::jni::JNIList::FromJava<std::shared_ptr<YR::Libruntime::Buffer>>(
        env, values, [](JNIEnv *env, jobject obj) -> std::shared_ptr<YR::Libruntime::Buffer> {
            return YR::jni::JNIByteBuffer::FromJava(env, static_cast<jbyteArray>(obj));
        });
    auto cmSetParam = YR::jni::JNIMSetParam::FromJava(env, mSetParam);

    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java
    auto cerrorInfo =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVMSetTx(ckeys, cvalues, cmSetParam);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return jerrorInfo;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVRead
 * Signature: (Ljava/lang/String;I)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVRead__Ljava_lang_String_2I(JNIEnv *env, jclass c,
                                                                                               jstring key,
                                                                                               jint timeoutMS)
{
    // parameters from java to cpp
    auto ckey = YR::jni::JNIString::FromJava(env, key);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    int ctimeoutMS = static_cast<int>(timeoutMS);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java (std::pair<std::shared_ptr<YR::Libruntime::Buffer>, ErrorInfo>)
    auto [sbuf_ptr, cerrorInfo] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVRead(ckey, ctimeoutMS);
    jbyteArray byteArray = nullptr;
    if (sbuf_ptr != nullptr) {
        byteArray = env->NewByteArray(sbuf_ptr->GetSize());
        env->SetByteArrayRegion(byteArray, 0, sbuf_ptr->GetSize(),
                                reinterpret_cast<const jbyte *>(sbuf_ptr->ImmutableData()));
    }
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return YR::jni::JNIPair::CreateJPair(env, byteArray, jerrorInfo);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVRead
 * Signature: (Ljava/util/List;IZ)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVRead__Ljava_util_List_2IZ(JNIEnv *env, jclass c,
                                                                                              jobject keys,
                                                                                              jint timeoutMS,
                                                                                              jboolean allowPartial)
{
    // parameters from java to cpp
    auto ckeys = YR::jni::JNIList::FromJava<std::string>(env, keys, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    int ctimeoutMS = static_cast<int>(timeoutMS);
    bool callowPartial = static_cast<jboolean>(allowPartial);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java (std::pair<std::vector<std::shared_ptr<YR::Libruntime::Buffer>>, ErrorInfo>)
    auto [sbuf_ptr_vector, cerrorInfo] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVRead(ckeys, ctimeoutMS, callowPartial);
    jobject listByteArray = YR::jni::JNIByteBuffer::FromCcPrtVectorToList(env, sbuf_ptr_vector);
    ASSERT_NOT_NULL(listByteArray);

    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return YR::jni::JNIPair::CreateJPair(env, listByteArray, jerrorInfo);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVGetWithParam
 * Signature: (Ljava/util/List;Lcom/yuanrong/Getparams;I)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVGetWithParam(JNIEnv *env, jclass c, jobject keys,
                                                                                 jobject getParams, jint timeoutMS)
{
    // parameters from java to cpp
    auto ckeys = YR::jni::JNIList::FromJava<std::string>(env, keys, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    int ctimeoutMS = static_cast<int>(timeoutMS);
    auto cgetParams = YR::jni::JNIGetParams::FromJava(env, getParams);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    auto [sbuf_ptr_vector, cerrorInfo] =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVGetWithParam(ckeys, cgetParams,
                                                                                           ctimeoutMS);
    jobject listByteArray = YR::jni::JNIByteBuffer::FromCcPrtVectorToList(env, sbuf_ptr_vector);
    ASSERT_NOT_NULL(listByteArray);

    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return YR::jni::JNIPair::CreateJPair(env, listByteArray, jerrorInfo);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVDel
 * Signature: (Ljava/lang/String;)Lcom/yuanrong/errorcode/ErrorInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVDel__Ljava_lang_String_2(JNIEnv *env, jclass c,
                                                                                             jstring key)
{
    // parameters from java to cpp
    auto ckey = YR::jni::JNIString::FromJava(env, key);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java (ErrorInfo)
    auto cerrorInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVDel(ckey);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    KVDel
 * Signature: (Ljava/util/List;)Lcom/yuanrong/errorcode/Pair;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KVDel__Ljava_util_List_2(JNIEnv *env, jclass c,
                                                                                           jobject keys)
{
    // parameters from java to cpp
    auto ckeys = YR::jni::JNIList::FromJava<std::string>(env, keys, [](JNIEnv *env, jobject obj) -> std::string {
        return YR::jni::JNIString::FromJava(env, static_cast<jstring>(obj));
    });
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SetTenantIdWithPriority();
    // result from cpp to java (std::pair<std::vector<std::string>>, ErrorInfo>)
    auto [cvector_keys, cerrorInfo] = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->KVDel(ckeys);
    jobject keysDeleted = YR::jni::JNIString::FromCcVectorToList(env, cvector_keys);
    ASSERT_NOT_NULL(keysDeleted);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, cerrorInfo);
    ASSERT_NOT_NULL(jerrorInfo);

    return YR::jni::JNIPair::CreateJPair(env, keysDeleted, jerrorInfo);
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    SaveState
 * Signature: (I;)Lcom/yuanrong/errorcode/ErrorInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_SaveState(JNIEnv *env, jclass c, jint timeoutMs)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);

    std::shared_ptr<YR::Libruntime::Buffer> data;
    auto errInfo = YR::jni::JNICodeExecutor::DumpInstance(env, "", data);
    if (!errInfo.OK()) {
        jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
        ASSERT_NOT_NULL(jerrorInfo);
        return jerrorInfo;
    }

    errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SaveState(data, timeoutMs);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    LoadState
 * Signature: (I;)Lcom/yuanrong/errorcode/ErrorInfo;
 */
JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_LoadState(JNIEnv *env, jclass c, jint timeoutMs)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);

    std::shared_ptr<YR::Libruntime::Buffer> data;
    jobject jerrorInfo;
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->LoadState(data, timeoutMs);
    if (!errInfo.OK()) {
        jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
        ASSERT_NOT_NULL(jerrorInfo);
        return jerrorInfo;
    }

    errInfo = YR::jni::JNICodeExecutor::LoadInstance(env, data);
    jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_GroupCreate(JNIEnv *env, jclass c, jstring str,
                                                                              jobject opt)
{
    auto groupOpts = YR::jni::JNIGroupOptions::FromJava(env, opt);
    auto cStr = YR::jni::JNIString::FromJava(env, str);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GroupCreate(cStr, groupOpts);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_GroupTerminate(JNIEnv *env, jclass c, jstring str)
{
    auto cStr = YR::jni::JNIString::FromJava(env, str);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GroupTerminate(cStr);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_GroupWait(JNIEnv *env, jclass c, jstring str)
{
    auto cStr = YR::jni::JNIString::FromJava(env, str);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GroupWait(cStr);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_processLog(JNIEnv *env, jclass c, jobject functionLog)
{
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto cFunctionLog = YR::jni::JNIFunctionLog::FromJava(env, functionLog);
    auto errInfo = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->ProcessLog(cFunctionLog);
    jobject jerrorInfo = YR::jni::JNIErrorInfo::FromCc(env, errInfo);
    ASSERT_NOT_NULL(jerrorInfo);
    return jerrorInfo;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    GetInstanceRoute
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_yuanrong_jni_LibRuntime_GetInstanceRoute(JNIEnv *env, jclass c,
                                                                                   jstring objectID)
{
    std::string cobjectID = YR::jni::JNIString::FromJava(env, objectID);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto instanceRoute =
        YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->GetInstanceRoute(cobjectID);
    jstring jinstanceRoute = YR::jni::JNIString::FromCc(env, instanceRoute);
    ASSERT_NOT_NULL(jinstanceRoute);

    return jinstanceRoute;
}

/*
 * Class:     com_yuanrong_jni_LibRuntime
 * Method:    SaveInstanceRoute
 * Signature: (Ljava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_yuanrong_jni_LibRuntime_SaveInstanceRoute(JNIEnv *env, jclass c,
                                                                                 jstring objectID,
                                                                                 jstring instanceRoute)
{
    std::string cobjectID = YR::jni::JNIString::FromJava(env, objectID);
    std::string cinstanceRoute = YR::jni::JNIString::FromJava(env, instanceRoute);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->SaveInstanceRoute(cobjectID, cinstanceRoute);
}

JNIEXPORT jobject JNICALL Java_com_yuanrong_jni_LibRuntime_KillSync(JNIEnv *env, jclass c, jstring instanceID)
{
    auto instID = YR::jni::JNIString::FromJava(env, instanceID);
    auto result = get_runtime_context_callback(env, c);
    auto rtCtx = YR::jni::JNIString::FromJava(env, (jstring)result);
    auto err = YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime(rtCtx)->Kill(
        instID, libruntime::Signal::killInstanceSync);
    jobject jerr = YR::jni::JNIErrorInfo::FromCc(env, err);
    if (jerr == nullptr) {
        YR::jni::JNILibruntimeException::ThrowNew(env, "failed to convert jerr when Libruntime_Kill, get null");
        return nullptr;
    }
    return jerr;
}

#ifdef __cplusplus
}
#endif
