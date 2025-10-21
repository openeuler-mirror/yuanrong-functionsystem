/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
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

package com.yuanrong.call;

import com.yuanrong.InvokeOptions;
import com.yuanrong.api.InvokeArg;
import com.yuanrong.api.YR;
import com.yuanrong.exception.YRException;
import com.yuanrong.function.CppFunction;
import com.yuanrong.function.CrossLangFunction;
import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.libruntime.generated.Libruntime.LanguageType;
import com.yuanrong.runtime.client.ObjectRef;
import com.yuanrong.utils.SdkUtils;

import java.util.List;

/**
 * Class for creating instances of stateless functions in C++.
 *
 * @note The CppFunctionHandler class is a handle for a C++ function created on the cloud by a Java function.
 *       It is the return type of the interface `YR.function(CppFunction<R> func)`.\n
 *       Users can use CppFunctionHandler to create and invoke C++ function instances.
 *
 * @since 2022/08/26
 */
public class CppFunctionHandler<R> {
    private final CrossLangFunction<R> func;

    private InvokeOptions options = new InvokeOptions();

    private String functionUrn = "";

    /**
     * CppFunctionHandler constructor.
     *
     * @param func CppFunction class instance.
     */
    public CppFunctionHandler(CppFunction<R> func) {
        this.func = func;
    }

    /**
     * Cpp function call interface.
     *
     * @param args The input parameters required to call the specified method.
     * @return ObjectRef: The "id" of the method's return value in the data system. Use YR.get() to get the actual
     *         return value of the method.
     * @throws YRException Unified exception types thrown.
     *
     * @snippet{trimleft} CppFunctionExample.java CppFunctionHandler invoke 样例代码
     */
    public ObjectRef invoke(Object... args) throws YRException {
        String functionID = "";
        if (!functionUrn.isEmpty()) {
            functionID = SdkUtils.reformatFunctionUrn(functionUrn);
        }
        FunctionMeta functionMeta = FunctionMeta.newBuilder()
                .setClassName("")
                .setFunctionName(func.functionName)
                .setFunctionID(functionID)
                .setSignature("")
                .setLanguage(LanguageType.Cpp)
                .setApiType(ApiType.Function)
                .build();
        List<InvokeArg> packedArgs = SdkUtils.packInvokeArgs(args);
        String objId = YR.getRuntime().invokeByName(functionMeta, packedArgs, options);
        return new ObjectRef(objId, func.returnType);
    }

    /**
     * When Java calls a stateless function in C++, set the functionUrn for the function.
     *
     * @param urn functionUrn, can be obtained after the function is deployed.
     * @return CppFunctionHandler<R>, with built-in invoke method, can create and call the cpp function instance.
     */
    public CppFunctionHandler<R> setUrn(String urn) {
        this.functionUrn = urn;
        return this;
    }

    /**
     * Used to dynamically modify the parameters of the called function.
     *
     * @param opt See InvokeOptions for details.
     * @return CppFunctionHandler<R>, Handles processed by C++ functions.
     */
    public CppFunctionHandler<R> options(InvokeOptions opt) {
        this.options = new InvokeOptions(opt);
        return this;
    }
}
