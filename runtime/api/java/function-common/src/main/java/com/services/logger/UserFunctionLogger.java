/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
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

package com.services.logger;

import com.services.runtime.RuntimeLogger;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * UserFunctionLogger
 *
 * @since 2024/7/22
 */
public class UserFunctionLogger implements RuntimeLogger {
    /**
     * userLogger name at log4j2.xml
     */
    private static final Logger logger = LogManager.getLogger("userLogger");

    private static final Set<String> LOG_LEVELS = new HashSet<>();

    static {
        LOG_LEVELS.addAll(Arrays.asList("DEBUG", "INFO", "WARN", "ERROR"));
    }

    private String logLevel = "INFO";

    /**
     * log with msg
     *
     * @param msg log info
     */
    @Override
    public void log(String msg) {
        switch (this.logLevel) {
            case "DEBUG":
                this.debug(msg);
                break;
            case "WARN":
                this.warn(msg);
                break;
            case "ERROR":
                this.error(msg);
                break;
            default:
                this.info(msg);
        }
    }

    /**
     * info with msg
     *
     * @param msg log info
     */
    @Override
    public void info(String msg) {
        logger.info(msg);
    }

    /**
     * debug with msg
     *
     * @param msg log info
     */
    @Override
    public void debug(String msg) {
        logger.debug(msg);
    }

    /**
     * warm with msg
     *
     * @param msg log info
     */
    @Override
    public void warn(String msg) {
        logger.warn(msg);
    }

    /**
     * error with msg
     *
     * @param msg log info
     */
    @Override
    public void error(String msg) {
        logger.error(msg);
    }

    /**
     * set level
     *
     * @param level log level
     */
    @Override
    public void setLevel(String level) {
        if (LOG_LEVELS.contains(level)) {
            this.logLevel = level;
        }
    }
}
