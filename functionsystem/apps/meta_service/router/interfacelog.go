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

package router

import (
	"strings"
	"sync"

	"meta_service/common/logger"
	"meta_service/common/logger/log"
)

const (
	splitStr = "|"
)

var (
	once            sync.Once
	interfaceLogger *logger.InterfaceLogger
)

// InterfaceLog interface log
type interfaceLog struct {
	httpMethod  string
	ip          string
	requestPath string
	query       string
	bussinessID string
	traceID     string
	retCode     string
	costTime    string
}

func initInterfaceLog() {
	cfg := logger.InterfaceEncoderConfig{ModuleName: "meta-service"}
	var err error
	interfaceLogger, err = logger.NewInterfaceLogger("", "meta-service-interface", cfg)
	if err != nil {
		log.GetLogger().Errorf("failed to init interface log")
	}
}

func write(i interfaceLog) {
	logStr := []string{i.httpMethod, i.ip, i.requestPath, i.query, i.bussinessID, i.traceID, i.retCode, i.costTime}

	once.Do(func() {
		if interfaceLogger == nil {
			initInterfaceLog()
		}
	})
	if interfaceLogger != nil {
		interfaceLogger.Write(strings.Join(logStr, splitStr))
	}
}
