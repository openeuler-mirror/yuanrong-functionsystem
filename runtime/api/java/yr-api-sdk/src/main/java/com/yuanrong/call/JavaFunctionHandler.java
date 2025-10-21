/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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
import com.yuanrong.function.CrossLangFunction;
import com.yuanrong.function.JavaFunction;
import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.libruntime.generated.Libruntime.LanguageType;
import com.yuanrong.runtime.client.ObjectRef;
import com.yuanrong.utils.SdkUtils;

import java.util.List;

/**
 * Create an operation class for creating a stateless Java function instance.
 *
 * @note The JavaFunctionHandler class is a handle for creating Java function instances; it is the return type of the
 *       `function (JavaFunction<R> javaFunction)` interface.\n
 *       Users can use JavaFunctionHandler to create and invoke Java function instances.
 *
 * @since 2024/04/16
 */
public class JavaFunctionHandler<R> {
    private final CrossLangFunction<R> func;

    private InvokeOptions options = new InvokeOptions();

    private String functionUrn = "";

    /**
     * JavaFunctionHandler constructor.
     *
     * @param func JavaFunction class instance.
     */
    public JavaFunctionHandler(JavaFunction<R> func) {
        this.func = func;
    }

    /**
     * Java function call interface.
     *
     * @param args The input parameters required to call the specified method.
     * @return ObjectRef: The "id" of the method's return value in the data system. Use YR.get() to get the actual
     *         return value of the method.
     * @throws YRException Unified exception types thrown.
     */
    public ObjectRef invoke(Object... args) throws YRException {
        String functionID = "";
        if (!functionUrn.isEmpty()) {
            functionID = SdkUtils.reformatFunctionUrn(functionUrn);
        }
        FunctionMeta functionMeta = FunctionMeta.newBuilder()
                .setClassName(func.className)
                .setFunctionName(func.functionName)
                .setFunctionID(functionID)
                .setSignature("")
                .setLanguage(LanguageType.Java)
                .setApiType(ApiType.Function)
                .build();
        List<InvokeArg> packedArgs = SdkUtils.packInvokeArgs(args);
        String objId = YR.getRuntime().invokeByName(functionMeta, packedArgs, options);
        return new ObjectRef(objId, func.returnType);
    }

    /**
     * When Java calls a stateless function in Java, set the functionUrn for the function.
     *
     * @param urn functionUrn, can be obtained after the function is deployed.
     * @return JavaFunctionHandler<R>, with built-in invoke method, can create and invoke the java function instance.
     *
     * @snippet{trimleft} SetUrnExample.java set urn of java invoke java stateless function
     */
    public JavaFunctionHandler<R> setUrn(String urn) {
        this.functionUrn = urn;
        return this;
    }

    /**
     * Used to dynamically modify the parameters of the called function.
     *
     * @param opt See InvokeOptions for details.
     * @return JavaFunctionHandler<R>, Handle processed by Java function.
     *
     * @snippet{trimleft} JavaInstanceExample.java JavaFunctionHandle options 样例代码
     */
    public JavaFunctionHandler<R> options(InvokeOptions opt) {
        this.options = new InvokeOptions(opt);
        return this;
    }
}
