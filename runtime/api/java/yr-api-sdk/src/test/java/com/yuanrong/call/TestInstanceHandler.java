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

import com.yuanrong.Config;
import com.yuanrong.api.YR;
import com.yuanrong.exception.YRException;
import com.yuanrong.function.YRFunc0;
import com.yuanrong.function.YRFunc1;
import com.yuanrong.function.YRFunc2;
import com.yuanrong.function.YRFunc3;
import com.yuanrong.function.YRFunc4;
import com.yuanrong.function.YRFunc5;
import com.yuanrong.function.YRFuncVoid0;
import com.yuanrong.function.YRFuncVoid1;
import com.yuanrong.function.YRFuncVoid2;
import com.yuanrong.function.YRFuncVoid3;
import com.yuanrong.function.YRFuncVoid4;
import com.yuanrong.function.YRFuncVoid5;
import com.yuanrong.libruntime.generated.Libruntime.ApiType;
import com.yuanrong.runtime.ClusterModeRuntime;
import com.yuanrong.serialization.Serializer;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.modules.junit4.PowerMockRunner;

import java.io.IOException;
import java.util.Map;

/**
 * Description: test case of InstanceHandler.
 *
 * @since 2024/05/20
 */
@RunWith(PowerMockRunner.class)
@PowerMockIgnore({"javax.net.ssl.*", "javax.management.*"})
@PrepareForTest(YR.class)
public class TestInstanceHandler {
    ClusterModeRuntime runtime = PowerMockito.mock(ClusterModeRuntime.class);

    @Before
    public void init() throws Exception {
        Config conf = new Config(
                "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
                "127.0.0.0",
                "127.0.0.0",
                "");
        PowerMockito.whenNew(ClusterModeRuntime.class).withAnyArguments().thenReturn(runtime);
        YR.init(conf);
    }

    @After
    public void finalizeYR() throws YRException {
        YR.Finalize();
    }

    @Test
    public void testInstanceHandlerConstructor() {
        String instanceId = "instanceId";
        ApiType apiType = ApiType.Function;
        InstanceHandler instanceHandler1 = new InstanceHandler(instanceId, apiType);
        InstanceHandler instanceHandler2 = new InstanceHandler();

        Assert.assertNotNull(instanceHandler1);
        Assert.assertNotNull(instanceHandler1.getInstanceId());
        Assert.assertNotNull(instanceHandler1.getApiType());

        Assert.assertNotNull(instanceHandler2);
        Assert.assertNull(instanceHandler2.getInstanceId());
        Assert.assertNull(instanceHandler2.getApiType());
    }

    @Test
    public void testInstanceHandlerOperator_000() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc0<Object>) () -> null);
        instanceHandler.function((YRFuncVoid0) () -> { });
        instanceHandler.clearHandlerInfo();
        Assert.assertNotNull(instanceHandler);
    }

    @Test
    public void testInstanceHandlerOperator_001() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc1<Object, Object>) o -> null);
        instanceHandler.function((YRFuncVoid1<Object>) o -> {
        });
        boolean isException = false;
        try{
            instanceHandler.terminate();
            instanceHandler.terminate(true);
            instanceHandler.terminate(false);
        } catch (YRException exp) {
            exp.printStackTrace();
            isException = true;
        }
        Assert.assertFalse(isException);
    }

    @Test
    public void testInstanceHandlerOperator_002() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc2<Object, Object, Object>) (o, o2) -> null);
        instanceHandler.function((YRFuncVoid2) (o, o2) -> {
        });
    }

    @Test
    public void testInstanceHandlerOperator_003() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc3<Object, Object, Object, Object>) (o, o2, o3) -> null);
        instanceHandler.function((YRFuncVoid3<Object, Object, Object>) (o, o2, o3) -> {
        });
    }

    @Test
    public void testInstanceHandlerOperator_004() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc4<Object, Object, Object, Object, Object>) (o, o2, o3, o4) -> null);
        instanceHandler.function((YRFuncVoid4<Object, Object, Object, Object>) (o, o2, o3, o4) -> {
        });
    }

    @Test
    public void testInstanceHandlerOperator_005() {
        InstanceHandler instanceHandler = new InstanceHandler();
        instanceHandler.function((YRFunc5<Object, Object, Object, Object, Object, Object>) (o, o2, o3, o4, o5) -> null);
        instanceHandler.function((YRFuncVoid5<Object, Object, Object, Object, Object>) (o, o2, o3, o4, o5) -> {
        });
    }

    @Test
    public void testInstanceHandlerPacker() throws IOException {
        String instanceId = "instanceId";
        ApiType apiType = ApiType.Function;
        InstanceHandler instanceHandler = new InstanceHandler(instanceId, apiType);
        byte[] bytes = Serializer.serialize(instanceHandler);
        if (Serializer.deserialize(bytes, InstanceHandler.class) instanceof InstanceHandler) {
            InstanceHandler unpackHandler = (InstanceHandler) Serializer.deserialize(bytes, InstanceHandler.class);
            Assert.assertEquals(unpackHandler.getInstanceId(), instanceId);
            Assert.assertEquals(unpackHandler.getApiType(), apiType);
        }
    }

    @Test
    public void testExportImportInstanceHandler() {
        String instanceId = "instanceId";
        ApiType apiType = ApiType.Function;
        boolean isException = false;
        InstanceHandler instanceHandler = new InstanceHandler(instanceId, apiType);
        instanceHandler.setNeedOrder(true);
        try {
            Map<String, String> mp = instanceHandler.exportHandler();
            Assert.assertEquals(mp.get("instanceKey"), instanceId);
            Assert.assertEquals(mp.get("apiType"), String.valueOf(apiType.getNumber()));
            Assert.assertEquals(mp.get("needOrder"), "true");
            mp.put("instanceID", "realInsID");
            mp.put("instanceRoute", "route");
            InstanceHandler newHandler = new InstanceHandler();
            newHandler.importHandler(mp);
            Assert.assertEquals(newHandler.getApiType(), apiType);
            Assert.assertEquals(newHandler.getInstanceId(), instanceId);
            Assert.assertEquals(newHandler.getRealInstanceId(), "realInsID");
            Assert.assertEquals(newHandler.isNeedOrder(), true);
        } catch (YRException exp) {
            exp.printStackTrace();
            isException = true;
        }
        Assert.assertFalse(isException);
    }

    @Test
    public void testInstanceHandlerHelper() {
        InstanceHandlerHelper instanceHandlerHelper = new InstanceHandlerHelper();
        Assert.assertNotNull(instanceHandlerHelper);
    }
}
