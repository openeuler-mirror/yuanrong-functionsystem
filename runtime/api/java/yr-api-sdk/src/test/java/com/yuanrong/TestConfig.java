/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
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

package com.yuanrong;

import com.yuanrong.exception.YRException;

import org.junit.Assert;
import org.junit.Test;

public class TestConfig {
    @Test
    public void testInitConfig() {
        Config testConf1 = new Config(
            "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
            "127.0.0.0",
            "127.0.0.0",
            "sn:cn:yrk:12345678901234561234567890123456:function:0-test-hello:$latest",
            false);
        Config testConf2 = new Config(
            "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
            "127.0.0.0",
            1,
            "127.0.0.0",
            1,
            "sn:cn:yrk:12345678901234561234567890123456:function:0-test-hello:$latest");
        Config testConf3 = new Config(
            "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
            "127.0.0.0",
            "127.0.0.0",
            "sn:cn:yrk:12345678901234561234567890123456:function:0-test-hello:$latest",
            true);
        Config testConf4 = new Config.Builder()
            .cppFunctionURN("sn:cn:yrk:12345678901234561234567890123456:function:0-test-hello:$latest")
            .isDriver(true)
            .ns("test-ns")
            .logDir("/tmp")
            .logLevel("test-level")
            .enableDisConvCallStack(false)
            .rpcTimeout(1)
            .build();

        testConf1.setMaxLogSizeMb(10);
        testConf2.setMaxLogFileNum(10);
        testConf4.toString();
        Assert.assertNotEquals(testConf3, testConf2);
        Assert.assertNotEquals(testConf1, testConf2);
    }

    @Test
    public void testCheckParameter() {
        Config testConf = new Config(
            "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest",
            "127.0.0.0",
            "127.0.0.0",
            "sn:cn:yrk:12345678901234561234567890123456:function:0-test-hello:$latest",
            true);
        boolean isException = false;

        testConf.setCppFunctionURN("test-cppFunction");
        try {
            testConf.checkParameter();
        } catch (YRException e) {
            Assert.assertTrue(e.getMessage().contains("cppFunctionURN is invalid"));
            testConf.setCppFunctionURN(
                "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest");
            isException = true;
        }
        Assert.assertTrue(isException);

        isException = false;
        testConf.setServerAddress("test-server-address");
        try {
            testConf.checkParameter();
        } catch (YRException e) {
            Assert.assertTrue(e.getMessage().contains("serverAddress is invalid"));
            testConf.setServerAddress("127.0.0.1");
            isException = true;
        }
        Assert.assertTrue(isException);

        isException = false;
        testConf.setDataSystemAddress("test-data-address");
        try {
            testConf.checkParameter();
        } catch (YRException e) {
            Assert.assertTrue(e.getMessage().contains("dataSystemAddress is invalid"));
            testConf.setDataSystemAddress("127.0.0.1");
            isException = true;
        }
        Assert.assertTrue(isException);

        isException = false;
        testConf.setFunctionURN("test-function-urn");
        try {
            testConf.checkParameter();
        } catch (YRException e) {
            Assert.assertTrue(e.getMessage().contains("functionURN is invalid"));
            testConf.setFunctionURN(
                "sn:cn:yrk:12345678901234561234567890123456:function:0-crossyrlib-helloworld:$latest");
            isException = true;
        }
        Assert.assertTrue(isException);

        isException = false;
        testConf.setRecycleTime(0);
        try {
            testConf.checkParameter();
        } catch (YRException e) {
            Assert.assertTrue(e.getMessage().contains("recycleTime is invalid"));
            isException = true;
        }
        Assert.assertTrue(isException);
    }
}
