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

package com.yuanrong.runtime.server;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.io.File;

/**
 * RuntimeLogger
 *
 * @since 2024/4/25
 */
public class RuntimeLogger {
    private static Logger LOG = null;
    private static final String ENV_GLOG_DIR = "GLOG_log_dir";
    private static final String DEFAULT_LOG_PATH = "/home/snuser/log/";
    private static final String DEFAULT_LOG_LEVEL = "INFO";
    private static final String EXCEPTION_LOG_PATH = "exception";
    private static final String ENV_LOG_LEVEL = "logLevel";
    private static final String ENV_JAVA_LOG_PATH = "logPath";
    private static final String ENV_EXCEPTION_LOG_DIR = "java.io.tmpdir";

    /**
     * initLogger
     * initialize logger
     *
     * @param runtimeID runtimeID
     */
    public static void initLogger(String runtimeID) {
        String logDir = System.getenv(ENV_GLOG_DIR);
        if (logDir == null || logDir.trim().isEmpty()) {
            logDir = DEFAULT_LOG_PATH;
        }
        String logPathName = logDir + File.separator + runtimeID;
        String logPathException = logDir + File.separator + EXCEPTION_LOG_PATH;
        String logLevel = System.getProperty(ENV_LOG_LEVEL);
        if (logLevel == null || logLevel.trim().isEmpty()) {
            logLevel = DEFAULT_LOG_LEVEL;
        }
        System.setProperty(ENV_LOG_LEVEL, logLevel);
        System.setProperty(ENV_JAVA_LOG_PATH, logPathName);
        System.setProperty(ENV_EXCEPTION_LOG_DIR, logPathException);
        LOG = LogManager.getLogger(RuntimeLogger.class);
        LOG.info("runtime ID {}", runtimeID);
        LOG.debug("current log path = {}", System.getProperty("logPath"));
        LOG.debug("current log level = {}", System.getProperty("logLevel"));
    }
}
