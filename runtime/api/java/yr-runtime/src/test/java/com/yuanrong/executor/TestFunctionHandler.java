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
package com.yuanrong.executor;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;
import org.powermock.api.support.membermodification.MemberModifier;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.modules.junit4.PowerMockRunner;
import org.powermock.modules.junit4.PowerMockRunnerDelegate;

import com.yuanrong.errorcode.ErrorCode;
import com.yuanrong.errorcode.ErrorInfo;
import com.yuanrong.libruntime.generated.Libruntime;
import com.yuanrong.libruntime.generated.Libruntime.FunctionMeta;
import com.yuanrong.serialization.Serializer;

/**
 * The type Http task test.
 *
 * @since 2024 /04/06
 */
@RunWith(PowerMockRunner.class)
@PowerMockIgnore("javax.management.*")
@PowerMockRunnerDelegate(JUnit4.class)
public class TestFunctionHandler {
    /**
     * Description：
     *   Confirm that the code and message from 'Create' method are correct.
     * Steps:
     *   1. Sets 'Create' method to throw an Exception
     * Expectation:
     *   The code and message return by the method are correct.
     * @throws Exception
     */
    @Test
    public void testCreateExeceptionWithCause() throws Exception {
        FunctionMeta meta = FunctionMeta.newBuilder().setClassName("MockClassName").setSignature("MockSignature")
                .setFunctionName("MockMethodName").build();
        HandlerIntf handler = new FunctionHandler();
        ReturnType returnType = handler.execute(meta, Libruntime.InvokeType.CreateInstanceStateless, null);
        assertEquals(returnType.getCode(), ErrorCode.ERR_USER_FUNCTION_EXCEPTION);
        assertTrue(returnType.getErrorInfo().getErrorMessage().contains("failed to create instance due to the cause:"));
    }

    /**
     * Description：
     *   The message should be in detailed when runtime failed to find Method.
     * Steps:
     *   1. Sets a 'MockClass' in classCache.
     *   2. Sets an invalid Signature for the 'mockMethod' in 'MockClass'.
     *   3. Calls 'invoke' method in 'FunctionHandler'.
     * Expectation:
     *   The message in returnType are in detailed.
     *
     * @throws Exception
     */
    @Test
    public void testInvokeWithFailedtoFindMethod() throws Exception {
        FunctionHandler handler = new FunctionHandler();
        handler.classCache.put("MockClass", MockClass.class);

        FunctionMeta meta = FunctionMeta.newBuilder()
                .setClassName("MockClass")
                .setFunctionName("mockMethod")
                .setSignature("invalidSignature")
                .build();
        ReturnType returnType = handler.execute(meta, Libruntime.InvokeType.InvokeFunctionStateless, null);
        assertTrue(returnType.getErrorInfo().getErrorMessage().contains("Failed to find user-definded method:"));
    }

    /**
     * Description：
     *   The message should be in detailed when runtime failed to find Method.
     * Steps:
     *   1. Sets a 'MockClass' in classCache.
     *   2. Sets the 'mockMethodwithException' in 'MockClass' throwing an exception.
     *   3. Calls 'invoke' method in 'FunctionHandler'.
     * Expectation:
     *   The Code in returnType should be ERR_USER_FUNCTION_EXCEPTION(2002).
     *
     * @throws Exception
     */
    @Test
    public void testInvokeWithUserFuncFailure() throws Exception {
        FunctionHandler handler = new FunctionHandler();
        handler.classCache.put("MockClass", MockClass.class);
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, new MockClass());
        FunctionMeta meta = FunctionMeta.newBuilder()
                .setClassName("MockClass")
                .setFunctionName("mockMethodWithException")
                .setSignature("()V")
                .build();
        ArrayList<ByteBuffer> args = new ArrayList<ByteBuffer>();
        ReturnType returnType = handler.execute(meta, Libruntime.InvokeType.InvokeFunction, args);
        assertEquals(ErrorCode.ERR_USER_FUNCTION_EXCEPTION, returnType.getCode());
    }

    /**
     * Description：
     *   The errorInfo of the returnType when invoke udf with no exception should have
     *   empty message.
     * Steps:
     *   1. Sets a 'MockClass' in classCache.
     *   2. Invokes the 'mockMethod' in 'MockClass'.
     *   3. Calls 'invoke' method in 'FunctionHandler'.
     * Expectation:
     *   The message in returnType should be an empty string.
     *
     * @throws Exception
     */
    @Test
    public void testReturnTypeMesgEmpty() throws Exception {
        FunctionHandler handler = new FunctionHandler();
        handler.classCache.put("MockClass", MockClass.class);
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, new MockClass());
        FunctionMeta meta = FunctionMeta.newBuilder()
                .setClassName("MockClass")
                .setFunctionName("mockMethod")
                .setSignature("()V")
                .build();
        ArrayList<ByteBuffer> args = new ArrayList<ByteBuffer>();
        ReturnType returnType = handler.execute(meta, Libruntime.InvokeType.InvokeFunction, args);
        assertEquals("", returnType.getErrorInfo().getErrorMessage());
    }

    @Test
    public void loadInstanceMismatchedInputException() throws ClassNotFoundException, IOException {
        String clzName = "MockClass";
        FunctionHandler.classCache.put(clzName, MockClass.class);
        byte[] clzNameBytes = Serializer.serialize(clzName);

        MockClass mockClass = new MockClass();
        byte[] clzByte = Serializer.serialize(mockClass);

        boolean isException = false;
        FunctionHandler handler = new FunctionHandler();
        try {
            handler.loadInstance(clzByte, clzNameBytes);
        } catch (IOException e) {
            isException = true;
            assertTrue(e.toString().contains("Catch MismatchedInputException while running LoadInstance"));
        }
        assertTrue(isException);
    }

    @Test
    public void testDumpInstance() {
        boolean isException = false;
        FunctionHandler handler = new FunctionHandler();
        try {
            handler.dumpInstance("testID");
        } catch (Exception e) {
            isException = true;
        }
        assertFalse(isException);
    }

    @Test
    public void testShutdown() throws Exception {
        boolean isException = false;
        FunctionHandler handler = new FunctionHandler();
        handler.classCache.put("MockClass", MockClass.class);
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, new MockClass());
        MemberModifier.field(FunctionHandler.class, "instanceClassName").set(FunctionHandler.class, "MockClass");
        try {
            handler.shutdown(10);
        } catch (Exception e) {
            isException = true;
        }
        assertFalse(isException);
    }

    @Test
    public void testShutdownFailed() throws Exception {
        FunctionHandler handler = new FunctionHandler();
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, null);
        ErrorInfo err = handler.shutdown(10);
        assertTrue(err.getErrorMessage().contains("Failed to invoke instance function"));
    }

    @Test
    public void testRecover() throws Exception {
        boolean isException = false;
        FunctionHandler handler = new FunctionHandler();
        handler.classCache.put("MockClass", MockClass.class);
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, new MockClass());
        MemberModifier.field(FunctionHandler.class, "instanceClassName").set(FunctionHandler.class, "MockClass");
        try {
            handler.recover();
        } catch (Exception e) {
            isException = true;
        }
        assertFalse(isException);
    }

    @Test
    public void testRecoverFailed() throws Exception {
        FunctionHandler handler = new FunctionHandler();
        MemberModifier.field(FunctionHandler.class, "instance").set(FunctionHandler.class, null);
        ErrorInfo err = handler.recover();
        assertTrue(err.getErrorMessage().contains("Failed to invoke instance function"));
    }
}