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

package com.yuanrong.call;

import com.yuanrong.InvokeOptions;
import com.yuanrong.api.YR;
import com.yuanrong.exception.YRException;
import com.yuanrong.function.YRFuncR;
import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.runtime.Runtime;
import com.yuanrong.runtime.util.Constants;
import com.yuanrong.utils.SdkUtils;

import lombok.Data;
import lombok.EqualsAndHashCode;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Creates an operation class for creating a Java stateful function instance.
 *
 * @note The instanceCreator is the creator of Java class instances; it is the return type of the interface
 *       `YR.instance(YRFuncR<A> func)`.\n
 *       Users can use the invoke method of instanceCreator to create Java class instances and return the
 *       InstanceHandler class handle.
 *
 * @since 2022/08/30
 */
@Data
@EqualsAndHashCode(callSuper = false)
public class InstanceCreator<A> extends Handler {
    private static Logger LOGGER = LoggerFactory.getLogger(InstanceCreator.class);

    private InvokeOptions options = new InvokeOptions();

    private final YRFuncR<A> func;

    private String name = "";

    private String nameSpace = "";

    private ApiType apiType;

    /**
     * The constructor of InstanceCreator.
     *
     * @param func YRFuncR class instance, Java function name.
     */
    public InstanceCreator(YRFuncR<A> func) {
        this(func, "", "", ApiType.Function);
    }

    /**
     * The constructor of InstanceCreator.
     *
     * @param func YRFuncR class instance, Java function name.
     * @param apiType The enumeration class has two values: Function and Posix.
     *                It is used internally by Yuanrong to distinguish function types. The default is Function.
     */
    public InstanceCreator(YRFuncR<A> func, ApiType apiType) {
        this(func, "", "", apiType);
    }

    /**
     * The constructor of InstanceCreator.
     *
     * @param func YRFuncR class instance, Java function name.
     * @param name The instance name of a named instance. When only name exists, the instance name will be set to name.
     * @param nameSpace Namespace of the named instance. When both name and nameSpace exist, the instance name will be
     *                  concatenated into nameSpace-name. This field is currently only used for concatenation, and
     *                  namespace isolation and other related functions will be completed later.
     */
    public InstanceCreator(YRFuncR<A> func, String name, String nameSpace) {
        this(func, name, nameSpace, ApiType.Function);
    }

    /**
     * The constructor of InstanceCreator.
     *
     * @param func YRFuncR class instance, Java function name.
     * @param name The instance name of a named instance. When only name exists, the instance name will be set to name.
     * @param nameSpace Namespace of the named instance. When both name and nameSpace exist, the instance name will be
     *                  concatenated into nameSpace-name. This field is currently only used for concatenation, and
     *                  namespace isolation and other related functions will be completed later.
     * @param apiType The enumeration class has two values: Function and Posix.
     *                It is used internally by Yuanrong to distinguish function types. The default is Function.
     */
    public InstanceCreator(YRFuncR<A> func, String name, String nameSpace, ApiType apiType) {
        this.func = func;
        this.name = name;
        this.nameSpace = nameSpace;
        this.apiType = apiType;
    }

    /**
     * The member method of the InstanceCreator class is used to create a Java class instance.
     *
     * @param args The input parameters required to create a class instance.
     * @return InstanceHandler Class handle.
     * @throws YRException Unified exception types thrown.
     */
    public InstanceHandler invoke(Object... args) throws YRException {
        FunctionMeta functionMeta = getFunctionMeta(this.func, this.apiType, this.nameSpace, this.name);
        String concurrency = options.getCustomExtensions().getOrDefault(Constants.CONCURRENCY, "1");
        if ("1".equals(concurrency)) {
            options.setNeedOrder(true);
        }
        Runtime runtime = YR.getRuntime();
        String instanceId = runtime.createInstance(functionMeta, SdkUtils.packInvokeArgs(args), options);
        InstanceHandler handler = new InstanceHandler(instanceId, apiType);
        handler.setNeedOrder(options.isNeedOrder());
        runtime.collectInstanceHandlerInfo(handler);
        return handler;
    }

    /**
     * The member method of the InstanceCreator class is used to dynamically modify the parameters for creating a Java
     * function instance.
     *
     * @param opt Function call options, used to specify functions such as calling resources.
     * @return InstanceCreator Class handle.
     *
     * @snippet{trimleft} OptionsExample.java instanceCreator options 样例代码
     */
    public InstanceCreator<A> options(InvokeOptions opt) {
        this.options = new InvokeOptions(opt);
        return this;
    }
}
