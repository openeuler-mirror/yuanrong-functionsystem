/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

package com.yuanrong.executor;

import com.yuanrong.errorcode.ErrorCode;
import com.yuanrong.errorcode.ModuleCode;
import com.yuanrong.exception.YRException;

public class MockClass{
    private int cnt = 0;

    public int getCausingDeserializationFailure() {
        return cnt;
    }

    public void mockMethod() {
        return;
    }

    public void mockMethodWithException() throws YRException {
        throw new YRException(ErrorCode.ERR_BUS_DISCONNECTION, ModuleCode.CORE, "");
    }

    public static void yrRecover() {
        return;
    }

    public static void yrShutdown(int gracePeriodSeconds) {
        return;
    }
}