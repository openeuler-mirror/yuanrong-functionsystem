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

package com.yuanrong.exception.handler.traceback;

import com.yuanrong.errorcode.ErrorCode;
import com.yuanrong.errorcode.ErrorInfo;
import com.yuanrong.errorcode.Pair;
import com.yuanrong.exception.YRException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

/**
 * Utils for stackTrace
 *
 * @since 2023 /12/25
 */
public class StackTraceUtils {
    private static final Logger LOGGER = LoggerFactory.getLogger(StackTraceUtils.class);

    private static final String RUNTIME_EXCEPTION_EXECUTE_TAG
        = "at com.yuanrong.codemanager.CodeExecutor.Execute";

    /**
     * This means that count backwards 6 lines up from at com.yuanrong.codemanager.CodeExecutor.Execute
     * that's all exceptions stacktrace of runtime
     */
    private static final int RUNTIME_EXCEPTION_EXECUTE_COUNT = 6;
    private static final String SUPPRESSED_EXCEPTION_BEGIN_TAG = "Caused by:";
    private static final String RUNTIME_CLASS_PATH = "com.yuanrong";
    private static final String JAVA_EXCEPTION = "java.lang.Exception";
    private static final String GO_FILE_PREFIX = "./";
    private static final int GO_PREFIX_INDEX = 2;

    private static final String REGEX_BEFORE = "((\\w+\\.?)+\\w+)\\.(\\w+)";
    private static final String REGEX_AFTER = "(\\w+\\.java):(\\d+)";
    private static final String AT_PATTERN = "^\\tat.*$";

    private static final int METHOD_MATCHER_NUMBER = 3;
    private static final int FILE_MATCHER_NUMBER = 2;

    /**
     * Check error and throw.
     *
     * @param errorInfo the error info
     * @param msg       the msg
     * @throws YRException the YR exception
     */
    public static void checkErrorAndThrowForInvokeException(ErrorInfo errorInfo, String msg) throws YRException {
        if (errorInfo.getErrorCode().equals(ErrorCode.ERR_OK)) {
            return;
        }
        if (errorInfo.getErrorCode().equals(ErrorCode.ERR_USER_FUNCTION_EXCEPTION)) {
            // process exception of user code
            List<StackTraceInfo> stackTraceInfos = errorInfo.getStackTraceInfos();
            LOGGER.error("occurs exception: {}, ErrorCode:{}, stackTraceInfo number:{} ", msg, errorInfo.getErrorCode(),
                    stackTraceInfos.size());
            if (stackTraceInfos.size() == 0) {
                throw new YRException(errorInfo);
            }
            Exception exception = fromStackTraceInfoListToException(stackTraceInfos);
            throw new YRException(errorInfo.getErrorCode(),
                errorInfo.getModuleCode(), errorInfo.getErrorMessage(), exception);
        } else {
            // process exception of yuanrong
            throw new YRException(errorInfo);
        }
    }

    private static Exception fromStackTraceInfoListToException(List<StackTraceInfo> infoList) {
        return assembleThrowable(infoList.get(0));
    }

    private static Exception assembleThrowable(StackTraceInfo info) {
        String className = info.getType().isEmpty() ? JAVA_EXCEPTION : info.getType();
        String message = info.getMessage();
        try {
            Class<Exception> clazz = (Class<Exception>) Class.forName(className);
            Constructor<Exception> constructor = clazz.getConstructor(String.class);
            Exception exp = constructor.newInstance(message);
            if (info.getStackTraceElements().size() > 0) {
                exp.setStackTrace(
                    formJavaStackTraceElement(info.getStackTraceElements()).toArray(new StackTraceElement[0]));
            }
            return exp;
        } catch (ClassNotFoundException | NoSuchMethodException | SecurityException | InstantiationException
                | IllegalAccessException | InvocationTargetException e) {
            throw new RuntimeException("Unable to find " + className + ", messsage:" + message, e);
        }
    }

    private static List<java.lang.StackTraceElement> formJavaStackTraceElement(List<StackTraceElement> traces) {
        List<java.lang.StackTraceElement> result = new ArrayList<java.lang.StackTraceElement>();
        for (StackTraceElement trace : traces) {
            if (trace == null) {
                continue;
            }
            result.add(new java.lang.StackTraceElement(trace.getClassName().isEmpty() ? "" : trace.getClassName(),
                trace.getMethodName().isEmpty() ? "" : trace.getMethodName(), trace.getFileName().isEmpty()
                ? ""
                : trace.getFileName().startsWith(GO_FILE_PREFIX)
                    ? trace.getFileName().substring(GO_PREFIX_INDEX)
                    : trace.getFileName(), trace.getLineNumber()));
        }
        return result;
    }

    /**
     * Process valid and duplicate list.
     *
     * @param trace the trace
     * @return the list
     */
    public static List<StackTraceElement> processValidAndDuplicate(List<StackTraceElement> trace) {
        if (trace.size() > 0) {
            List<StackTraceElement> filteredStackTraceElements = trace.stream()
                    .filter(element -> element != null && !element.getClassName().isEmpty())
                    .collect(Collectors.toList());
            Set<StackTraceElement> uniqueElements = new LinkedHashSet<>(filteredStackTraceElements);
            return new ArrayList<>(uniqueElements);
        }

        return Collections.emptyList();
    }

    /**
     * Gets list trace elements.
     *
     * @param printedStackTrace the printed stack trace
     * @return the list trace elements
     */
    public static List<StackTraceElement> getListTraceElements(String printedStackTrace) {
        List<StackTraceElement> result = new ArrayList<>();
        if (printedStackTrace == null || printedStackTrace.isEmpty()) {
            LOGGER.error("throwable object have not any stack information");
            return result;
        }

        String[] strArray = printedStackTrace.split(System.lineSeparator());

        int suppressedExceptionBeginIndex = getSuppressedExceptionBeginIndex(strArray, printedStackTrace);
        Pair<Integer, Boolean> returnValue = getRuntimeExceptionBeginIndex(strArray);
        int rtExceptionIndex = returnValue.getFirst();
        boolean isFoundExecuteStr = returnValue.getSecond();

        LOGGER.debug("get suppressedExceptionBeginIndex:{}, rtExceptionIndex:{}, strArray.length:{} ",
                suppressedExceptionBeginIndex, rtExceptionIndex, strArray.length);

        if (!printedStackTrace.contains(SUPPRESSED_EXCEPTION_BEGIN_TAG)) {
            // skip exception type message
            suppressedExceptionBeginIndex = 1;
        }

        Pattern pattern = Pattern.compile(AT_PATTERN, Pattern.MULTILINE);

        if (rtExceptionIndex == strArray.length - RUNTIME_EXCEPTION_EXECUTE_COUNT) {
            // RUNTIME_EXCEPTION_BEGIN_TAG
            LOGGER.debug("rtExceptionIndex == strArray.length - RUNTIME_EXCEPTION_EXECUTE_COUNT : {}, {}",
                    rtExceptionIndex, strArray.length - RUNTIME_EXCEPTION_EXECUTE_COUNT);
            for (int j = suppressedExceptionBeginIndex; j < rtExceptionIndex; j++) {
                Matcher matcher = pattern.matcher(strArray[j]);
                if (!strArray[j].contains(RUNTIME_CLASS_PATH) && matcher.find()) {
                    result.add(stringToStackTraceEle(strArray[j].trim()));
                }
            }
        } else if (rtExceptionIndex == strArray.length && isFoundExecuteStr) {
            LOGGER.debug("rtExceptionIndex == strArray.length : {}, {}",
                    rtExceptionIndex, strArray.length);
            int end = 0;
            if (rtExceptionIndex - RUNTIME_EXCEPTION_EXECUTE_COUNT > 0) {
                end = rtExceptionIndex - RUNTIME_EXCEPTION_EXECUTE_COUNT;
            } else {
                end = strArray.length;
            }
            for (int j = suppressedExceptionBeginIndex; j < end; j++) {
                Matcher matcher = pattern.matcher(strArray[j]);
                if (!strArray[j].contains(RUNTIME_CLASS_PATH) && matcher.find()) {
                    result.add(stringToStackTraceEle(strArray[j].trim()));
                }
            }
        } else {
            LOGGER.debug("rtExceptionIndex and strArray.length : {}, {}", rtExceptionIndex, strArray.length);
            for (int j = suppressedExceptionBeginIndex; j < rtExceptionIndex; j++) {
                Matcher matcher = pattern.matcher(strArray[j]);
                if (!strArray[j].contains(RUNTIME_CLASS_PATH) && matcher.find()) {
                    result.add(stringToStackTraceEle(strArray[j].trim()));
                }
            }

            for (int j = 1; j < rtExceptionIndex; j++) {
                Matcher matcher = pattern.matcher(strArray[j]);
                if (matcher.find()) {
                    result.add(stringToStackTraceEle(strArray[j].trim()));
                }
            }
        }

        LOGGER.debug("before return print result: {}", result);
        return result;
    }

    /**
     * String to stack trace ele stack trace element.
     *
     * @param elementStr the element str
     * @return the stack trace element
     */
    public static StackTraceElement stringToStackTraceEle(String elementStr) {
        String[] splits = elementStr.split("\\(");
        if (splits.length < 2) {
            throw new RuntimeException("exception happened while parsing staceTraceElement string");
        }
        Matcher methodMatcher = getStackMatcher(REGEX_BEFORE, splits[0]);
        Matcher fileMatcher = getStackMatcher(REGEX_AFTER, splits[1]);

        if (methodMatcher.groupCount() < METHOD_MATCHER_NUMBER || fileMatcher.groupCount() < FILE_MATCHER_NUMBER) {
            throw new RuntimeException("exception happened while matching method and file info");
        }

        StackTraceElement stackTrace = null;
        if (methodMatcher.find() && fileMatcher.find()) {
            String className = methodMatcher.group(1);
            String methodName = methodMatcher.group(3);
            String fileName = fileMatcher.group(1);
            int lineNumber = Integer.parseInt(fileMatcher.group(2));
            stackTrace = new StackTraceElement(className, methodName, fileName, lineNumber);
        }
        return stackTrace;
    }

    private static Matcher getStackMatcher(String regex, String stackStr) {
        Pattern pattern = Pattern.compile(regex);
        return pattern.matcher(stackStr);
    }

    private static Pair<Integer, Boolean> getRuntimeExceptionBeginIndex(String[] strArray) {
        LOGGER.debug("getRuntimeExceptionBeginIndexForward strArray.length = {}", strArray.length);
        int rtExceptionIndex = 0;
        boolean isFoundExecuteStr = false;
        for (rtExceptionIndex = 0; rtExceptionIndex < strArray.length; rtExceptionIndex++) {
            String strTemp = strArray[rtExceptionIndex];
            if (strTemp.isEmpty() || strTemp.contains(RUNTIME_EXCEPTION_EXECUTE_TAG)) {
                // find "at com.yuanrong.codemanager.CodeExecutor.Execute"
                LOGGER.debug("strTemp before break loop:{}, rtExceptionIndex:{}",
                        strTemp, rtExceptionIndex);
                isFoundExecuteStr = true;
                break;
            }
        }
        LOGGER.debug("rtExceptionIndex={} after break loop, strArray.length - 1:{}",
                rtExceptionIndex, strArray.length - 1);
        if (rtExceptionIndex > RUNTIME_EXCEPTION_EXECUTE_COUNT && isFoundExecuteStr) {
            rtExceptionIndex = rtExceptionIndex - RUNTIME_EXCEPTION_EXECUTE_COUNT + 1;
        }
        return new Pair<>(rtExceptionIndex, isFoundExecuteStr);
    }

    /**
     * Get java caused by exception begin index
     *
     * @param strArray the strArray
     * @param printedStackTrace the printedStackTrace
     * @return int
     */
    private static int getSuppressedExceptionBeginIndex(String[] strArray, String printedStackTrace) {
        int suppressedExceptionBeginIndex = 0;

        if (!printedStackTrace.contains(SUPPRESSED_EXCEPTION_BEGIN_TAG)) {
            return suppressedExceptionBeginIndex;
        }
        for (String str : strArray) {
            if (str != null && str.indexOf(SUPPRESSED_EXCEPTION_BEGIN_TAG) == -1) {
                suppressedExceptionBeginIndex++;
            } else {
                break;
            }
        }
        return suppressedExceptionBeginIndex;
    }
}
