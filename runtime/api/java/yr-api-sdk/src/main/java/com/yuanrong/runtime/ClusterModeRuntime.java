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

package com.yuanrong.runtime;

import com.yuanrong.Config;
import com.yuanrong.CreateParam;
import com.yuanrong.FunctionWrapper;
import com.yuanrong.GetParams;
import com.yuanrong.GroupOptions;
import com.yuanrong.InvokeOptions;
import com.yuanrong.MSetParam;
import com.yuanrong.SetParam;
import com.yuanrong.api.InvokeArg;
import com.yuanrong.api.YR;
import com.yuanrong.call.CppInstanceHandler;
import com.yuanrong.call.InstanceHandler;
import com.yuanrong.call.JavaInstanceHandler;
import com.yuanrong.errorcode.ErrorCode;
import com.yuanrong.errorcode.ErrorInfo;
import com.yuanrong.errorcode.ModuleCode;
import com.yuanrong.errorcode.Pair;
import com.yuanrong.exception.YRException;
import com.yuanrong.exception.LibRuntimeException;
import com.yuanrong.exception.handler.traceback.StackTraceUtils;
import com.yuanrong.jni.LibRuntime;
import com.yuanrong.jni.LibRuntimeConfig;
import com.yuanrong.libruntime.generated.Libruntime;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.runtime.client.KVManager;
import com.yuanrong.runtime.client.ObjectRef;
import com.yuanrong.runtime.util.Constants;
import com.yuanrong.runtime.util.Utils;
import com.yuanrong.serialization.Serializer;
import com.yuanrong.serialization.strategy.Strategy;
import com.yuanrong.storage.InternalWaitResult;
import com.yuanrong.utils.SdkUtils;

import lombok.Getter;

import org.apache.commons.lang3.tuple.ImmutablePair;
import org.objectweb.asm.Type;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.stream.Collectors;

/**
 * Cluster mode runtime implementation
 *
 * @since 2023 /10/16
 */
public class ClusterModeRuntime implements Runtime {
    private static final Logger LOGGER = LoggerFactory.getLogger(ClusterModeRuntime.class);

    /**
     * The Constructor name.
     */
    private static final String CONSTRUCTOR_NAME = "<init>";

    /**
     * The Functions.
     */
    ConcurrentMap<String, Map<org.apache.commons.lang3.tuple.Pair<String, String>,
        org.apache.commons.lang3.tuple.Pair<FunctionWrapper, Boolean>>>
        functions = new ConcurrentHashMap<>();

    /**
     * The Inheritance info.
     */
    @Getter
    Map<String, HashSet<String>> inheritanceInfo = new ConcurrentHashMap<>();

    /**
     * The Java instance handler map.
     */
    Map<String, InstanceHandler> instanceHandlerMap = new ConcurrentHashMap<>();

    /**
     * The Java instance handler map.
     */
    Map<String, JavaInstanceHandler> javaInstanceHandlerMap = new ConcurrentHashMap<>();

    /**
     * The Cpp instance handler map.
     */
    Map<String, CppInstanceHandler> cppInstanceHandlerMap = new ConcurrentHashMap<>();

    private final ReentrantReadWriteLock rwLock = new ReentrantReadWriteLock();

    private final Lock rLock = rwLock.readLock();

    private final Lock wLock = rwLock.writeLock();

    /**
     * Runtime configuration
     */
    private Config config;

    /**
     * Instantiates a new Cluster mode runtime.
     */
    public ClusterModeRuntime() {}

    @Override
    public void init(LibRuntimeConfig config) throws YRException {
        LOGGER.debug("cluster mode runtime init get java functionIds: {}",
            config.getFunctionIds().get(Libruntime.LanguageType.Java));
        ErrorInfo errorInfo = LibRuntime.Init(config);
        Utils.checkErrorAndThrow(errorInfo, "failed to init libruntime");
        LOGGER.debug("succeed to init libruntime)");
    }

    @Override
    public List<Object> get(List<ObjectRef> refs, int timeoutSec, boolean allowPartial) throws YRException {
        if (timeoutSec < Constants.NO_TIMEOUT) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                    "get config timeout (" + timeoutSec + ") is invalid");
        }
        List<String> refIds = refs.stream().map(ObjectRef::getObjectID).collect(Collectors.toList());
        long timeoutMs = Constants.NO_TIMEOUT;
        if (timeoutSec != Constants.NO_TIMEOUT) {
            timeoutMs = (long) timeoutSec * Constants.SEC_TO_MS;
        }
        Pair<ErrorInfo, List<byte[]>> getRes;
        try {
            // This method may throw 'LibruntimeException' when timeout occurs.
            getRes = LibRuntime.Get(refIds, (int) timeoutMs, allowPartial);
        } catch (LibRuntimeException e) {
            throw new YRException(e.getErrorCode(), e.getModuleCode(), e.getMessage());
        } catch (Exception e) {
            throw new YRException(ErrorCode.ERR_GET_OPERATION_FAILED, ModuleCode.RUNTIME, e);
        }
        StackTraceUtils.checkErrorAndThrowForInvokeException(getRes.getFirst(), getRes.getFirst().getErrorMessage());
        List<byte[]> res = getRes.getSecond();
        return Strategy.getObjects(res, refs);
    }

    @Override
    public Object get(ObjectRef ref, int timeoutSec) throws YRException {
        if (ref == null) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, "ObjectRef cannot be null");
        }
        List<ObjectRef> refs = Collections.singletonList(ref);
        List<Object> objects;
        try {
            objects = get(refs, timeoutSec, false);
        } catch (YRException e) {
            SdkUtils.filterGetTraceInfo(e);
            throw e;
        }

        return objects.size() > 0 ? objects.get(0) : null;
    }

    @Override
    public ObjectRef put(Object obj, long interval, long retryTime) throws YRException {
        ByteBuffer byteBuffer;
        try {
            byteBuffer = Strategy.selectSerializationStrategy(obj).serialize(obj);
        } catch (IOException e) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, e);
        }
        List<String> nestObjIds = new ArrayList<>(Serializer.CONTAINED_OBJECT_IDS.get());
        Pair<ErrorInfo, String> res = LibRuntime.Put(byteBuffer.array(), nestObjIds);
        Utils.checkErrorAndThrow(res.getFirst(), "put object");
        ObjectRef ret = new ObjectRef(res.getSecond());
        ret.setByteBuffer((obj instanceof ByteBuffer));
        ret.setType(obj.getClass());
        return ret;
    }

    @Override
    public ObjectRef put(Object obj, long interval, long retryTime, CreateParam createParam)
        throws YRException {
        ByteBuffer byteBuffer;
        try {
            byteBuffer = Strategy.selectSerializationStrategy(obj).serialize(obj);
        } catch (IOException e) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, e);
        }
        List<String> nestObjIds = new ArrayList<>(Serializer.CONTAINED_OBJECT_IDS.get());
        Pair<ErrorInfo, String> res = LibRuntime.PutWithParam(byteBuffer.array(), nestObjIds, createParam);
        Utils.checkErrorAndThrow(res.getFirst(), "put object");
        ObjectRef ret = new ObjectRef(res.getSecond());
        ret.setByteBuffer((obj instanceof ByteBuffer));
        ret.setType(obj.getClass());
        return ret;
    }

    @Override
    public InternalWaitResult wait(List<String> objIds, int waitNum, int timeoutSec) throws YRException {
        if (timeoutSec <= 0 && timeoutSec != Constants.NO_TIMEOUT) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                    "timeout should be larger than 0 or be -1");
        }
        if (checkRepeated(objIds)) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                    "duplicate objectRef exists in objs list");
        }
        if (waitNum == 0) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, "waitNum cannot be 0");
        }
        InternalWaitResult waitResult = LibRuntime.Wait(objIds, waitNum, timeoutSec);
        if (waitResult == null) {
            throw new YRException(ErrorCode.ERR_INNER_SYSTEM_ERROR, ModuleCode.RUNTIME,
                    "failed to get wait result");
        }
        if (waitResult.getExceptionIds().size() > 0) {
            LOGGER.warn("wait objIds, exception ids size is {}", waitResult.getExceptionIds().size());
            Iterator<Map.Entry<String, ErrorInfo>> it = waitResult.getExceptionIds().entrySet().iterator();
            Map.Entry<String, ErrorInfo> entry = it.next();
            StackTraceUtils.checkErrorAndThrowForInvokeException(entry.getValue(),
                "wait objIds(" + entry.getKey() + "...)");
        }
        return waitResult;
    }

    private boolean checkRepeated(List<String> objIds) {
        Set<String> sets = new HashSet<>();
        for (String id : objIds) {
            if (sets.contains(id)) {
                return true;
            }
            sets.add(id);
        }
        return false;
    }

    /**
     * Stop runtime.
     */
    static void stopRuntime() {
    }

    /**
     * Invoke function by function name as method name in Java.
     *
     * @param functionMeta the function meta
     * @param args         the args
     * @param opt          the opt
     * @return string of object id
     * @throws YRException the YR exception.
     */
    @Override
    public String invokeByName(
            FunctionMeta functionMeta, List<InvokeArg> args, InvokeOptions opt) throws YRException {
        String language = functionMeta.getLanguage().name();
        LOGGER.debug("start invoke function by name({}), language({})", functionMeta.getFunctionName(), language);
        Pair<ErrorInfo, String> res = LibRuntime.InvokeInstance(functionMeta, "", args, opt);
        Utils.checkErrorAndThrow(res.getFirst(), "invoke function");
        String objId = res.getSecond();
        LOGGER.debug("succeed to invoke function by name({}), language({}), objId({})", functionMeta.getFunctionName(),
                language, objId);
        return objId;
    }

    @Override
    public ObjectRef invokeJavaByName(FunctionMeta functionMeta, InvokeOptions opt, Object... args)
        throws YRException {
        FunctionWrapper function = YR.getRuntime().getJavaFunction(functionMeta);
        SdkUtils.checkJavaParameterTypes(function, args);
        String objId = this.invokeByName(functionMeta, SdkUtils.packInvokeArgs(args), opt);
        Class<?> returnType = function.getReturnType().orElse(null);
        return new ObjectRef(objId, returnType);
    }

    /**
     * Invokes a function on a specific instance.
     *
     * @param functionMeta The metadata of the function to be invoked.
     * @param instanceId   The ID of the instance on which the function will be
     *                     invoked.
     * @param args         The list of arguments to be passed to the function.
     * @param opt          The invoke options to be used for the function
     *                     invocation.
     * @return The result of the function invocation.
     * @throws YRException if an error occurs during the invocation.
     */
    @Override
    public String invokeInstance(FunctionMeta functionMeta, String instanceId, List<InvokeArg> args,
            InvokeOptions opt) throws YRException {
        LOGGER.debug("start to invoke instance({}) functionMeta is {}", instanceId, functionMeta);
        Pair<ErrorInfo, String> res = LibRuntime.InvokeInstance(functionMeta, instanceId, args, opt);
        Utils.checkErrorAndThrow(res.getFirst(), "invoke instance");
        String objId = res.getSecond();
        LOGGER.debug("succeed to invoke instance({}) objId({})", instanceId, objId);
        return objId;
    }

    /**
     * Create instance string.
     *
     * @param functionMeta the function meta
     * @param args         the args
     * @param opt          the opt
     * @return instanceId
     * @throws YRException the YR exception.
     */
    @Override
    public String createInstance(FunctionMeta functionMeta, List<InvokeArg> args, InvokeOptions opt)
        throws YRException {
        LOGGER.debug("start to create instance {}", functionMeta);
        Pair<ErrorInfo, String> res;
        try {
            res = LibRuntime.CreateInstance(functionMeta, args, opt);
        } catch (LibRuntimeException e) {
            throw new YRException(e.getErrorCode(), e.getModuleCode(), e.getMessage());
        }
        Utils.checkErrorAndThrow(res.getFirst(), "create instance");
        String instanceId = res.getSecond();
        LOGGER.info("succeed to create instance({})", instanceId);
        return instanceId;
    }

    /**
     * Cancel.
     *
     * @param objectId the object id
     */
    @Override
    public void cancel(String objectId) {
    }

    /**
     * Terminate instance.
     *
     * @param instanceId the instance id
     * @throws YRException the YR exception.
     */
    @Override
    public void terminateInstance(String instanceId) throws YRException {
        ErrorInfo errorInfo = LibRuntime.Kill(instanceId);
        Utils.checkErrorAndThrow(errorInfo, "kill instance(" + instanceId + ")");
        LOGGER.info("succeed to terminate instance({})", instanceId);
    }

    /**
     * Is on cloud boolean.
     *
     * @return the boolean
     */
    @Override
    public boolean isOnCloud() {
        return false;
    }

    /**
     * Get real instance id.
     *
     * @param objectId the object id
     * @return the string representing the real instance id
     */
    @Override
    public String getRealInstanceId(String objectId) {
        return LibRuntime.GetRealInstanceId(objectId);
    }

    /**
     * Save real instance id.
     *
     * @param objectId the object id
     * @param instanceId the instance id
     * @param opts the invoke options
     */
    @Override
    public void saveRealInstanceId(String objectId, String instanceId, InvokeOptions opts) {
        LibRuntime.SaveRealInstanceId(objectId, instanceId, opts);
    }

    /**
     * Collect instance handler info.
     *
     * @param instanceHandler the instance handler
     */
    @Override
    public void collectInstanceHandlerInfo(InstanceHandler instanceHandler) {
        instanceHandlerMap.put(instanceHandler.getInstanceId(), instanceHandler);
    }

    @Override
    public void collectInstanceHandlerInfo(JavaInstanceHandler javaInstanceHandler) {
        javaInstanceHandlerMap.put(javaInstanceHandler.getInstanceId(), javaInstanceHandler);
    }

    @Override
    public void collectInstanceHandlerInfo(CppInstanceHandler cppInstanceHandler) {
        cppInstanceHandlerMap.put(cppInstanceHandler.getInstanceId(), cppInstanceHandler);
    }

    /**
     * Returns an InstanceHandler object that contains the Java instance handler
     * which is NOT terminated and associated with the specified instanceID.
     *
     * @param instanceID The instanceID that identifies the Java instance handler
     * @return An InstanceHandler object associated with the specified instanceID
     */
    @Override
    public InstanceHandler getInstanceHandlerInfo(String instanceID) {
        return this.instanceHandlerMap.get(instanceID);
    }

    /**
     * Finalizes all actors and tasks and release any resources associated with
     * them.
     * Shuts down the KVManager Thread executor service. Any currently executing
     * tasks will be interrupted.
     */
    @Override
    public void Finalize() {
        LibRuntime.Finalize();
        clearInstanceHandlerInfo();
    }

    /**
     * In the runtimeCtx, finalizes all actors and tasks and release any resources
     * associated with them.
     * Shuts down the KVManager Thread executor service. Any currently executing
     * tasks will be interrupted.
     *
     * @param runtimeCtx the runtimeCtx
     * @param leftRuntimeNum the leftRuntimeNum
     */
    @Override
    public void Finalize(String runtimeCtx, int leftRuntimeNum) {
        LibRuntime.FinalizeWithCtx(runtimeCtx);
        if (leftRuntimeNum == 0) {
            clearInstanceHandlerInfo();
        }
    }

    /**
     * Exit the current instance. This function only supports in runtime.
     */
    @Override
    public void exit() {
        LibRuntime.Exit();
    }

    /**
     * Returns the key-value manager instance.
     *
     * @return The key-value manager instance
     */
    @Override
    public KVManager getKVManager() {
        return KVManager.getSingleton();
    }

    /**
     * Writes a key-value pair to the key-value store with the specified setParam
     * option.
     *
     * @param key       The key of the key-value pair
     * @param value     The value of the key-value pair
     * @param setParam  The setParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @throws YRException if an error occurs while performing the write operation
     */
    @Override
    public void KVWrite(String key, byte[] value, SetParam setParam) throws YRException {
        if (value == null) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                    "Cannot set a null value to key: " + key);
        }
        ErrorInfo errorInfo = LibRuntime.KVWrite(key, value, setParam);
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVWrite err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
                    errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
    }

    /**
     * Set Multiple key-value pairs.
     * Operations on all key-value pairs must either succeed or fail completely.
     *
     * @param keys      The keys of the key-value pair
     * @param values    The values of the key-value pair
     * @param mSetParam The mSetParam option for datasystem stateClient, contain writeMode,
     *                  ttlSecond, existence and cacheType.
     * @throws YRException if an error occurs while performing the write operation
     */
    @Override
    public void KVMSetTx(List<String> keys, List<byte[]> values, MSetParam mSetParam) throws YRException {
        for (int i = 0; i < keys.size(); ++i) {
            if (values.get(i) == null) {
                throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                        "Cannot set a null value to key: " + keys.get(i));
            }
        }
        ErrorInfo errorInfo = LibRuntime.KVMSetTx(keys, values, mSetParam);
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVMSetTx err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
                    errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
    }

    /**
     * Gets java function.
     *
     * @param functionMeta the descriptor
     * @return the function
     * @throws YRException the YR exception
     */
    @Override
    public FunctionWrapper getJavaFunction(FunctionMeta functionMeta) throws YRException {
        String className = functionMeta.getClassName();
        Map<org.apache.commons.lang3.tuple.Pair<String, String>,
            org.apache.commons.lang3.tuple.Pair<FunctionWrapper, Boolean>>
            allClassFunctions = functions.get(className);
        if (allClassFunctions == null) {
            synchronized (this) {
                allClassFunctions = functions.get(className);
                if (allClassFunctions == null) {
                    inheritanceInfo.putIfAbsent(className, new HashSet<>());
                    allClassFunctions = loadFunctionsForClass(className);
                    functions.putIfAbsent(className, allClassFunctions);
                }
            }
        }
        final org.apache.commons.lang3.tuple.Pair<String, String> key = ImmutablePair.of(functionMeta.getFunctionName(),
                functionMeta.getSignature());
        FunctionWrapper func = allClassFunctions.get(key).getLeft();
        if (func == null) {
            if (allClassFunctions.containsKey(key)) {
                throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                        String.format("FunctionWrapper %s is overloaded, the signature can't be empty.",
                                functionMeta.getFunctionName()));
            } else {
                throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME, String.format(
                        "FunctionWrapper %s not found", functionMeta.getFunctionName()));
            }
        }
        return func;
    }

    /**
     * Load functions for class map.
     *
     * @param className the class name
     * @return the map
     * @throws YRException the YR exception.
     */
    private Map<org.apache.commons.lang3.tuple.Pair<String, String>,
        org.apache.commons.lang3.tuple.Pair<FunctionWrapper, Boolean>> loadFunctionsForClass(
        String className) throws YRException {
        ClassLoader classLoader = getClass().getClassLoader();
        Class<?> clazz;
        try {
            clazz = Class.forName(className, true, classLoader);
        } catch (ClassNotFoundException e) {
            throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                "class not found:" + className);
        }
        List<Executable> executables = new ArrayList<>();
        executables.addAll(Arrays.asList(clazz.getDeclaredMethods()));
        executables.addAll(Arrays.asList(clazz.getDeclaredConstructors()));
        Class<?> clz = clazz;
        clz = clz.getSuperclass();
        while (clz != null && clz != Object.class) {
            executables.addAll(Arrays.asList(clz.getDeclaredMethods()));
            clz = clz.getSuperclass();
        }
        for (Class<?> baseInterface : clazz.getInterfaces()) {
            for (Method method : baseInterface.getDeclaredMethods()) {
                if (method.isDefault()) {
                    executables.add(method);
                }
            }
        }
        Collections.reverse(executables);
        Map<org.apache.commons.lang3.tuple.Pair<String, String>,
            org.apache.commons.lang3.tuple.Pair<FunctionWrapper, Boolean>> map = new HashMap<>();
        // method name, signature ;yrfunction, isDefault function in interface
        for (Executable executable : executables) {
            executable.setAccessible(true);
            final Type type;
            if (executable instanceof Method) {
                type = Type.getType((Method) executable);
            } else if (executable instanceof Constructor) {
                type = Type.getType((Constructor) executable);
            } else {
                throw new YRException(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                    "executable is not an instance of Method or Constructor");
            }
            final String signature = type.getDescriptor();
            String declaringClassName = executable.getDeclaringClass().getName();
            inheritanceInfo.get(className).add(declaringClassName);
            final String methodName = executable instanceof Method ? executable.getName() : CONSTRUCTOR_NAME;
            FunctionMeta meta = FunctionMeta.newBuilder().setClassName(declaringClassName)
                    .setFunctionName(methodName)
                    .setSignature(signature)
                    .setLanguage(Libruntime.LanguageType.Java).build();
            FunctionWrapper yrFunction = new FunctionWrapper(executable, meta);
            final boolean isDefault = executable instanceof Method && ((Method) executable).isDefault();
            map.put(ImmutablePair.of(methodName, signature), ImmutablePair.of(yrFunction, isDefault));
        }
        return map;
    }

    /**
     * Reads the value associated with the specified key from the key-value store.
     *
     * @param key       The key to read value for
     * @param timeoutMS The maximum time to wait for the read operation to complete, in
     *                  milliseconds
     * @return A byte array representing the value associated with the given key
     * @throws YRException If an error occurs while performing the read operation
     */
    @Override
    public byte[] KVRead(String key, int timeoutMS) throws YRException {
        Pair<byte[], ErrorInfo> result = LibRuntime.KVRead(key, timeoutMS);
        ErrorInfo errorInfo = result.getSecond();
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVRead err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
            errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
        return result.getFirst();
    }

    /**
     * Reads the values associated with the given keys from the key-value store.
     *
     * @param keys         The list of keys to read values for
     * @param timeoutMS    The maximum time to wait for the read operation to
     *                     complete, in milliseconds
     * @param allowPartial Specifies whether to allow partial reads or not
     * @return A list of byte arrays representing the values associated with the
     *         given keys
     * @throws YRException If an error occurs while performing the read operation
     */
    @Override
    public List<byte[]> KVRead(List<String> keys, int timeoutMS, boolean allowPartial) throws YRException {
        Pair<List<byte[]>, ErrorInfo> result = LibRuntime.KVRead(keys, timeoutMS, allowPartial);
        ErrorInfo errorInfo = result.getSecond();
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVRead err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
            errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
        return result.getFirst();
    }

    /**
     * Reads the values associated with the given keys and get params from the key-value store.
     *
     * @param keys The keys to retrieve values for
     * @param params The get params for datasystem
     * @param timeoutMS    The maximum time to wait for the read operation to
     *                     complete, in milliseconds
     * @return A list of byte arrays representing the values associated with the
     *         keys
     * @throws YRException if an error occurs while retrieving the values
     */
    @Override
    public List<byte[]> KVGetWithParam(List<String> keys, GetParams params, int timeoutMS) throws YRException {
        Pair<List<byte[]>, ErrorInfo> result = LibRuntime.KVGetWithParam(keys, params, timeoutMS);
        ErrorInfo errorInfo = result.getSecond();
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVGetWithParam err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(),
                    errorInfo.getModuleCode(), errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
        return result.getFirst();
    }

    /**
     * Deletes a key-value pair.
     *
     * @param key The key of the pair to be deleted.
     * @throws YRException If an error occurs while performing the delete operation
     */
    @Override
    public void KVDel(String key) throws YRException {
        ErrorInfo errorInfo = LibRuntime.KVDel(key);
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVDel err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
            errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
    }

    /**
     * Deletes multiple key-value pairs from the key-value store.
     *
     * @param keys A list of keys of the pairs to be deleted
     * @return A list of keys that were failed to be deleted
     */
    @Override
    public List<String> KVDel(List<String> keys) {
        Pair<List<String>, ErrorInfo> result = LibRuntime.KVDel(keys);
        ErrorInfo errorInfo = result.getSecond();
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("KVDel err: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(), errorInfo.getModuleCode(),
            errorInfo.getErrorMessage());
        }
        return result.getFirst();
    }

    /**
     * Load state of the runtime with a timeout.
     *
     * @param timeoutSec The maximum time to wait for the load operation to complete,
     *                   in seconds.
     * @throws YRException If an error occurs while performing the load operation.
     */
    @Override
    public void loadState(int timeoutSec) throws YRException {
        int timeoutMs = Constants.NO_TIMEOUT;
        if (timeoutSec != Constants.NO_TIMEOUT) {
            timeoutMs = timeoutSec * Constants.SEC_TO_MS;
        }
        ErrorInfo errorInfo = LibRuntime.LoadState(timeoutMs);
        Utils.checkErrorAndThrow(errorInfo, "Load state error");
    }

    /**
     * Saves state of the runtime with a timeout.
     *
     * @param timeoutSec The maximum time to wait for the save operation to complete,
     *                   in seconds.
     * @throws YRException If an error occurs while performing the save operation.
     */
    @Override
    public void saveState(int timeoutSec) throws YRException {
        int timeoutMs = Constants.NO_TIMEOUT;
        if (timeoutSec != Constants.NO_TIMEOUT) {
            timeoutMs = timeoutSec * Constants.SEC_TO_MS;
        }
        ErrorInfo errorInfo = LibRuntime.SaveState(timeoutMs);
        Utils.checkErrorAndThrow(errorInfo, "Save state error");
    }

    private void clearInstanceHandlerInfo() {
        for (Map.Entry<String, InstanceHandler> entry : instanceHandlerMap.entrySet()) {
            InstanceHandler instanceHandler = entry.getValue();
            instanceHandler.clearHandlerInfo();
        }
        for (Map.Entry<String, JavaInstanceHandler> entry : javaInstanceHandlerMap.entrySet()) {
            JavaInstanceHandler javaInstanceHandler = entry.getValue();
            javaInstanceHandler.clearHandlerInfo();
        }
        for (Map.Entry<String, CppInstanceHandler> entry : cppInstanceHandlerMap.entrySet()) {
            CppInstanceHandler cppInstanceHandler = entry.getValue();
            cppInstanceHandler.clearHandlerInfo();
        }
    }

    @Override
    public void groupCreate(String groupName, GroupOptions opts) throws YRException {
        if (opts.getTimeout() != -1 && opts.getTimeout() < 0) {
            ErrorInfo errorInfo = new ErrorInfo(ErrorCode.ERR_PARAM_INVALID, ModuleCode.RUNTIME,
                "The value of timeout should be -1 or greater than 0");
            throw new YRException(errorInfo);
        }
        ErrorInfo errorInfo = LibRuntime.GroupCreate(groupName, opts);
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("group create error: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(),
                errorInfo.getModuleCode(), errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
    }
    @Override
    public void groupWait(String groupName) throws YRException {
        ErrorInfo errorInfo = LibRuntime.GroupWait(groupName);
        if (!errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            LOGGER.error("group wait error: Code:{}, MCode:{}, Msg:{}", errorInfo.getErrorCode(),
                errorInfo.getModuleCode(), errorInfo.getErrorMessage());
            throw new YRException(errorInfo);
        }
    }

    @Override
    public void groupTerminate(String groupName) {
        LibRuntime.GroupTerminate(groupName);
    }

    /**
     * Get instance route.
     *
     * @param objectId the object id
     * @return the string representing the instance route
     */
    @Override
    public String getInstanceRoute(String objectId) {
        return LibRuntime.GetInstanceRoute(objectId);
    }

    /**
     * Save instance route.
     *
     * @param objectId the object id
     * @param instanceRoute the instance route
     */
    @Override
    public void saveInstanceRoute(String objectId, String instanceRoute) {
        LibRuntime.SaveInstanceRoute(objectId, instanceRoute);
    }

    /**
     * Sync terminate instance.
     *
     * @param instanceId the instance id
     * @throws YRException the YR exception.
     */
    @Override
    public void terminateInstanceSync(String instanceId) throws YRException {
        ErrorInfo errorInfo = LibRuntime.KillSync(instanceId);
        Utils.checkErrorAndThrow(errorInfo, "kill instance sync(" + instanceId + ")");
        LOGGER.info("succeed to terminate instance sync({})", instanceId);
    }
}
