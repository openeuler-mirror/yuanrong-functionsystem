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
import com.yuanrong.api.YR;
import com.yuanrong.exception.YRException;
import com.yuanrong.function.CppInstanceClass;
import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.libruntime.generated.Libruntime.LanguageType;
import com.yuanrong.runtime.Runtime;
import com.yuanrong.runtime.util.Constants;
import com.yuanrong.utils.SdkUtils;

/**
 * Create an operation class for creating a cpp stateful function instance.
 *
 * @note The CppInstanceCreator class is a Java function that creates C++ class instances;
 *       it is the return type of the interface `YR.instance(CppInstanceClass cppInstanceClass)`.\n
 *       Users can use the invoke method of CppInstanceCreator to create a C++ class instance and return the handle
 *       class CppInstanceHandler.
 *
 * @since 2022/08/26
 */
public class CppInstanceCreator {
    private final CppInstanceClass cppInstanceClass;

    private FunctionMeta functionMeta;

    private InvokeOptions options = new InvokeOptions();

    private String name;

    private String nameSpace;

    private String functionUrn = "";

    /**
     * The constructor of CppInstanceCreator.
     *
     * @param cppInstanceClass CppInstanceClass class instance.
     */
    public CppInstanceCreator(CppInstanceClass cppInstanceClass) {
        this(cppInstanceClass, "", "");
    }

    /**
     * The constructor of CppInstanceCreator.
     *
     * @param cppInstanceClass CppInstanceClass class instance.
     * @param name The instance name of a named instance. When only name exists, the instance name will be set to name.
     * @param nameSpace Namespace of the named instance. When both name and nameSpace exist, the instance name will be
     *                  concatenated into nameSpace-name. This field is currently only used for concatenation, and
     *                  namespace isolation and other related functions will be completed later.
     */
    public CppInstanceCreator(CppInstanceClass cppInstanceClass, String name, String nameSpace) {
        this.cppInstanceClass = cppInstanceClass;
        this.name = name;
        this.nameSpace = nameSpace;
    }

    /**
     * The member method of the CppInstanceHandler class is used to create a cpp class instance.
     *
     * @param args The input parameters required to create a class instance.
     * @return CppInstanceHandler Class handle.
     * @throws YRException Unified exception types thrown.
     */
    public CppInstanceHandler invoke(Object... args) throws YRException {
        String funcName = cppInstanceClass.className + "::" + cppInstanceClass.initFunctionName;
        String functionID = "";
        if (!functionUrn.isEmpty()) {
            functionID = SdkUtils.reformatFunctionUrn(functionUrn);
        }
        this.functionMeta = FunctionMeta.newBuilder()
                .setClassName(cppInstanceClass.className)
                .setFunctionName(funcName)
                .setFunctionID(functionID)
                .setSignature("")
                .setLanguage(LanguageType.Cpp)
                .setApiType(ApiType.Function)
                .setName(name)
                .setNs(nameSpace)
                .build();
        String concurrency = options.getCustomExtensions().getOrDefault(Constants.CONCURRENCY, "1");
        if ("1".equals(concurrency)) {
            options.setNeedOrder(true);
        }
        Runtime runtime = YR.getRuntime();
        String instanceId = runtime.createInstance(this.functionMeta, SdkUtils.packInvokeArgs(args), options);
        CppInstanceHandler cppInstanceHandler = new CppInstanceHandler(instanceId, functionID,
            this.cppInstanceClass.className);
        cppInstanceHandler.setNeedOrder(options.isNeedOrder());
        runtime.collectInstanceHandlerInfo(cppInstanceHandler);
        return cppInstanceHandler;
    }

    /**
     * When Java calls a stateful function in C++, set the functionUrn for the function.
     *
     * @param urn functionUrn, can be obtained after the function is deployed.
     * @return CppInstanceCreator, with built-in invoke method, can create instances of this cpp function class.
     *
     * @snippet{trimleft} SetUrnExample.java set urn of java invoke cpp stateful function
     */
    public CppInstanceCreator setUrn(String urn) {
        this.functionUrn = urn;
        return this;
    }

    /**
     * The member method of the CppInstanceCreator class is used to dynamically modify the parameters for creating
     * a C++ function instance.
     *
     * @param opt Function call options, used to specify functions such as calling resources.
     * @return CppInstanceCreator Class handle.
     *
     * @snippet{trimleft} CppFunctionExample.java CppInstanceCreator options 样例代码
     */
    public CppInstanceCreator options(InvokeOptions opt) {
        this.options = new InvokeOptions(opt);
        return this;
    }
}
