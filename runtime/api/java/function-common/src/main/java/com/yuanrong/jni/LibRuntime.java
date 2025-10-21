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

package com.yuanrong.jni;

import com.yuanrong.CreateParam;
import com.yuanrong.GetParams;
import com.yuanrong.GroupOptions;
import com.yuanrong.InvokeOptions;
import com.yuanrong.MSetParam;
import com.yuanrong.SetParam;
import com.yuanrong.api.InvokeArg;
import com.yuanrong.errorcode.ErrorInfo;
import com.yuanrong.errorcode.Pair;
import com.yuanrong.exception.LibRuntimeException;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.libruntime.generated.Socket.FunctionLog;
import com.yuanrong.runtime.config.RuntimeContext;
import com.yuanrong.storage.InternalWaitResult;

import java.util.List;

/**
 * NativeLibRuntime represents methods of libRuntime
 * Generating command:
 * > cd MultiLanguageRuntime/java
 * > javah -classpath yr-jni/src/main/java/:function-common/src/main/java -d function-common/src/main/cpp/
 * -jni com.yuanrong.jni.LibRuntime
 *
 * @since 2023/10/23
 */
public class LibRuntime {
    static {
        LoadUtil.loadLibrary();
    }

    /**
     * get RuntimeContext
     *
     * @return RuntimeContext
     */
    public static String GetRuntimeContext() {
        return RuntimeContext.RUNTIME_CONTEXT.get();
    }

    /**
     * Init libruntime
     *
     * @param config the config
     * @return the ErrorInfo
     */
    public static native ErrorInfo Init(LibRuntimeConfig config);

    /**
     * Create Instance
     *
     * @param functionMeta functionMeta
     * @param args         create arguments
     * @param opt          invoke options
     * @return Pair of ErrorInfo and String
     * @throws LibRuntimeException the LibRuntimeException
     */
    public static native Pair<ErrorInfo, String> CreateInstance(FunctionMeta functionMeta, List<InvokeArg> args,
            InvokeOptions opt) throws LibRuntimeException;

    /**
     * Invoke instance by id
     *
     * @param funcMeta      functionMeta
     * @param instanceId    instanceId
     * @param args          invoke arguments
     * @param opt           invoke options
     * @return Pair of ErrorInfo and String
     */
    public static native Pair<ErrorInfo, String> InvokeInstance(FunctionMeta funcMeta, String instanceId,
            List<InvokeArg> args, InvokeOptions opt);

    /**
     * Get invoke results with ObjectRefIds
     *
     * @param ids          objectRefIds
     * @param timeoutMs    timeout in seconds, will be transferred to milliseconds
     *                     in Get interface
     * @param allowPartial whether to allow partial results
     * @return Pair of ErrorInfo and List<byte[]>
     * @throws LibRuntimeException the LibRuntimeException
     */
    public static native Pair<ErrorInfo, List<byte[]>> Get(List<String> ids, int timeoutMs, boolean allowPartial)
        throws LibRuntimeException;

    /**
     * Native method for put data
     *
     * @param data the data
     * @param nestObjIds the nestObjIds
     * @return Pair of ErrorInfo and String
     */
    public static native Pair<ErrorInfo, String> Put(byte[] data, List<String> nestObjIds);

    /**
     * Native method for put data
     *
     * @param data the data
     * @param nestObjIds the nestObjIds
     * @param createParam create param of datasystem
     * @return Pair of ErrorInfo and String
     */
    public static native Pair<ErrorInfo, String> PutWithParam(byte[] data, List<String> nestObjIds,
            CreateParam createParam);

    /**
     * Native method for Wait
     *
     * @param ids the ids
     * @param waitNum the waitNum
     * @param timeoutSec the timeoutSec
     * @return InternalWaitResult
     */
    public static native InternalWaitResult Wait(List<String> ids, int waitNum, int timeoutSec);

    /**
     * Native method for ReceiveRequestLoop
     */
    public static native void ReceiveRequestLoop();

    /**
     * Native method for FinalizeWithCtx
     *
     * @param runtimeCtx the runtimeCtx
     */
    public static native void FinalizeWithCtx(String runtimeCtx);

    /**
     * Native method for Finalize
     */
    public static native void Finalize();

    /**
     * Native method for Exit
     */
    public static native void Exit();

    /**
     * Native method for IsInitialized
     *
     * @return IsInitialized
     */
    public static native boolean IsInitialized();

    /**
     * Native method for setRuntimeContext
     *
     * @param jobID jobID
     */
    public static native void setRuntimeContext(String jobID);

    /**
     * Native method for Kill instance
     *
     * @param instanceId the instanceId
     * @return ErrorInfo
     */
    public static native ErrorInfo Kill(String instanceId);

    /**
     * Native method for autoInitYR
     *
     * @param info info
     * @return YRAutoInitInfo
     */
    public static native YRAutoInitInfo AutoInitYR(YRAutoInitInfo info);

    /**
     * Native method for retrieving the real instance ID of an object.
     *
     * @param objectID The ID of the object whose real instance ID is to be
     *                 retrieved
     * @return The real instance ID of the object as a String
     */
    public static native String GetRealInstanceId(String objectID);

    /**
     * Native method for saving the real instance ID of an object.
     *
     * @param objectID the object id
     * @param instanceID the instance id
     * @param opts the invoke options
     */
    public static native void SaveRealInstanceId(String objectID, String instanceID, InvokeOptions opts);

    /**
     * Native method for writing key-value pairs to a key-value store.
     *
     * @param key       The key to write.
     * @param value     The value to write.
     * @param setParam  The setParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @return An ErrorInfo object containing information about any errors that
     *         occurred during the write operation.
     */
    public static native ErrorInfo KVWrite(String key, byte[] value, SetParam setParam);

    /**
     * Native method for writing key-value pairs to a key-value store.
     *
     * @param keys      The keys to write.
     * @param values    The values to write.
     * @param mSetParam The mSetParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @return An ErrorInfo object containing information about any errors that
     *         occurred during the write operation.
     */
    public static native ErrorInfo KVMSetTx(List<String> keys, List<byte[]> values, MSetParam mSetParam);

    /**
     * Native method for reading a single key-value pair.
     *
     * @param key       The key to look up
     * @param timeoutMS The maximum amount of time in milliseconds to wait for the read operation
     * @return A Pair object containing the byte array value associated with the
     *         key, and an ErrorInfo object if there was an error.
     */
    public static native Pair<byte[], ErrorInfo> KVRead(String key, int timeoutMS);

    /**
     * Native method for reading multiple key-value pairs.
     *
     * @param keys         A list of keys to look up in the key-value store.
     * @param timeoutMS    The maximum amount of time to wait for the key-value store to
     *                     respond.
     * @param allowPartial A boolean flag indicating whether to return partial
     *                     results if some of the keys are not found.
     * @return A Pair object containing a list of byte array values associated with
     *         the keys, and an ErrorInfo object if there was an error.
     */
    public static native Pair<List<byte[]>, ErrorInfo> KVRead(List<String> keys, int timeoutMS, boolean allowPartial);

    /**
     * Native method for reading multiple key-value pairs with get params.
     *
     * @param keys         A list of keys to look up in the key-value store.
     * @param params       The get params for datasystem.
     * @param timeoutMS    The maximum amount of time to wait for the key-value store to
     *                     respond.
     * @return A Pair object containing a list of byte array values associated with
     *         the keys, and an ErrorInfo object if there was an error.
     */
    public static native Pair<List<byte[]>, ErrorInfo> KVGetWithParam(List<String> keys,
            GetParams params, int timeoutMS);

    /**
     * Native method for deleting the value associated with the given key from the
     * key-value store.
     *
     * @param key The key of the value to be deleted
     * @return An ErrorInfo object indicating the success or failure of the
     *         operation
     */
    public static native ErrorInfo KVDel(String key);

    /**
     * Native method for deleting the values associated with the given keys from the
     * key-value store.
     *
     * @param keys The keys of the values to be deleted
     * @return A Pair object containing a List of keys that were successfully
     *         deleted and an instance of ErrorInfo indicating the success or
     *         failure of the operation
     */
    public static native Pair<List<String>, ErrorInfo> KVDel(List<String> keys);

    /**
     * Native method for SaveState
     *
     * @param timeoutMs the timeoutMs
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo SaveState(int timeoutMs);

    /**
     * Native method for LoadState
     *
     * @param timeoutMs the timeoutMs
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo LoadState(int timeoutMs);

    /**
     * Native method for GroupCreate
     *
     * @param groupName the groupName
     * @param opts the opts
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo GroupCreate(String groupName, GroupOptions opts);

    /**
     * Native method for GroupTerminate
     *
     * @param groupName the groupName
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo GroupTerminate(String groupName);

    /**
     * Native method for GroupWait
     *
     * @param groupName the groupName
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo GroupWait(String groupName);

    /**
     * Native method for processLog
     *
     * @param functionLog the functionLog
     * @return An ErrorInfo object indicating the success or failure of the operation
     */
    public static native ErrorInfo processLog(FunctionLog functionLog);

    /**
     * Native method for DecreaseReference
     *
     * @param ids the ids
     */
    public static native void DecreaseReference(List<String> ids);

    /**
     * Native method for retrieving the instance route of an object.
     *
     * @param objectID The ID of the object whose instance route is to be retrieved
     * @return The instance route of the object as a String
     */
    public static native String GetInstanceRoute(String objectID);

    /**
     * Native method for saving the instance route of an object.
     *
     * @param objectID the object id
     * @param instanceRoute the instance route
     */
    public static native void SaveInstanceRoute(String objectID, String instanceRoute);

    /**
     * Native method for Kill instance sync
     *
     * @param instanceId the instanceId
     * @return ErrorInfo
     */
    public static native ErrorInfo KillSync(String instanceId);
}
