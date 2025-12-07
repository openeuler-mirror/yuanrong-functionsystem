/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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
package functionhandler

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestBuildHandle(t *testing.T) {
	testCases := []struct {
		name                   string
		kind                   string
		runtime                string
		handler                string
		preStop                string
		initializer            string
		initializerTimeout     int
		preStopTimeout         int
		info                   FunctionHookHandlerInfo
		handlerWantStd         string
		hookHandlerWantNum     int
		extendedHandlerWantNum int
		extendedTimeoutWantNum int
	}{
		{
			name:                   "faas kind",
			runtime:                "python3.8",
			handler:                "handler.myhandler",
			preStop:                "",
			initializer:            "init",
			initializerTimeout:     900,
			kind:                   "faas",
			info:                   FunctionHookHandlerInfo{},
			handlerWantStd:         "handler.myhandler",
			hookHandlerWantNum:     6,
			extendedHandlerWantNum: 1,
			extendedTimeoutWantNum: 1,
		},
		{
			name:                   "yrlib kind",
			kind:                   "yrlib",
			runtime:                "python3.8",
			handler:                "handler.myhandler",
			info:                   FunctionHookHandlerInfo{},
			handlerWantStd:         "fusion_computation_handler.fusion_computation_handler",
			hookHandlerWantNum:     6,
			extendedHandlerWantNum: 0,
			extendedTimeoutWantNum: 0,
		},
		{
			name:                   "posix-runtime-custom kind",
			kind:                   "posix-runtime-custom",
			runtime:                "python3.8",
			handler:                "handler.myhandler",
			info:                   FunctionHookHandlerInfo{},
			handlerWantStd:         "",
			hookHandlerWantNum:     0,
			extendedHandlerWantNum: 0,
			extendedTimeoutWantNum: 0,
		},
		{
			name:                   "custom kind",
			kind:                   "custom",
			runtime:                "",
			handler:                "handler.myhandler",
			info:                   FunctionHookHandlerInfo{},
			handlerWantStd:         "",
			hookHandlerWantNum:     0,
			extendedHandlerWantNum: 0,
			extendedTimeoutWantNum: 0,
		},
		{
			name:                   "cpp11 kind",
			kind:                   "yrlib",
			runtime:                "cpp11",
			handler:                "handler.myhandler",
			info:                   FunctionHookHandlerInfo{},
			handlerWantStd:         "",
			hookHandlerWantNum:     0,
			extendedHandlerWantNum: 0,
			extendedTimeoutWantNum: 0,
		},
	}
	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			mapBuilder := GetBuilder(tt.kind, tt.runtime, tt.handler)
			handler := mapBuilder.Handler()
			assert.Equal(t, tt.handlerWantStd, handler)
			hookHandler := mapBuilder.HookHandler(tt.runtime, tt.info)
			assert.Equal(t, tt.hookHandlerWantNum, len(hookHandler))
			extendedHandler := mapBuilder.ExtendedHandler(tt.initializer, tt.preStop)
			assert.Equal(t, tt.extendedHandlerWantNum, len(extendedHandler))
			extendedTimeout := mapBuilder.ExtendedTimeout(tt.initializerTimeout, tt.preStopTimeout)
			assert.Equal(t, tt.extendedTimeoutWantNum, len(extendedTimeout))
		})
	}
}
