/*
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
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

// Package logwrap print logs by frequency.
package logwrap

import (
	"sync"

	"meta_service/common/logger/log"
)

const (
	// Frequency print frequency
	Frequency = 10
	// INFO info log level
	INFO = "INFO"
	// ERROR error log level
	ERROR = "ERROR"
	// DEBUG debug log level
	DEBUG = "DEBUG"
	// WARN warn log level
	WARN = "WARN"
)

var logWrap = &logWrapper{}

type logWrapper struct {
	// sync map key is runtimeID string
	// sync map value is nums
	logWrap sync.Map
}

// Store sets the value for the logString
func Store(logString string) {
	logWrapTime, exist := logWrap.logWrap.Load(logString)
	if !exist || logWrapTime == nil {
		logWrapTime = 0
	} else {
		logWrapTime = (logWrapTime.(int) + 1) % Frequency
	}
	logWrap.logWrap.Store(logString, logWrapTime)
}

// Delete deletes logWrap for the logString
func Delete(logString string) {
	logWrap.logWrap.Delete(logString)
}

// Print print logs by frequency and level
func Print(id string, level, format string, paras ...interface{}) {
	logWrapTime, exist := logWrap.logWrap.Load(id)
	if !exist || logWrapTime == nil {
		logWrapTime = 0
	}
	if logWrapTime.(int)%Frequency == 0 {
		switch level {
		case ERROR:
			log.GetLogger().Errorf(format, paras...)
		case INFO:
			log.GetLogger().Infof(format, paras...)
		case DEBUG:
			log.GetLogger().Debugf(format, paras...)
		case WARN:
			log.GetLogger().Warnf(format, paras...)
		default:
			log.GetLogger().Infof(format, paras...)
		}
	}
}
