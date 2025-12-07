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
	"fmt"
	"strings"
)

// handler map
const (
	// InitHandler Name of the initHandler in the hookHandler map
	InitHandler = "init"
	// CallHandler Name of the callHandler in the hookHandler map
	CallHandler = "call"
	// CheckpointHandler Name of the checkpointHandler in the hookHandler map
	CheckpointHandler = "checkpoint"
	// RecoverHandler Name of the recoverHandler in the hookHandler map
	RecoverHandler = "recover"
	// ShutdownHandler Name of the shutdownHandler in the hookHandler map
	ShutdownHandler = "shutdown"
	// SignalHandler Name of the signalHandler in the hookHandler map
	SignalHandler = "signal"
	// ExtendedHandler Name of the handler in the extendedHandler map
	ExtendedHandler = "handler"
	// ExtendedInitializer Name of the initializer in the extendedHandler map
	ExtendedInitializer = "initializer"
	// ExtendedPreStop Name of the pre_stop in the extendedHandler map
	ExtendedPreStop = "pre_stop"
	// ExtendedHandlers name of the handlers in the extendedHandler map
	ExtendedHandlers = "extendedHandler"
	// ExtendedTimeouts name of the initializer's timeout
	ExtendedTimeouts = "extendedTimeout"

	// Faas kind of function creation
	Faas = "faas"
	// Yrlib kind of function creation
	Yrlib = "yrlib"
	// PosixCustom kind of function creation
	PosixCustom = "posix-runtime-custom"
	// Custom kind of function creation
	Custom = "custom"
	// JavaRuntimePrefix java runtime prefix
	JavaRuntimePrefix = "java"
	// PythonRuntimePrefix python runtime prefix
	PythonRuntimePrefix = "python"
	// CppRuntimePrefix cpp runtime prefix
	CppRuntimePrefix = "cpp"
)

const (
	yrlibHandler  = "fusion_computation_handler.fusion_computation_handler"
	customHandler = ""
)

// FunctionHookHandlerInfo function hook handler info
type FunctionHookHandlerInfo struct {
	InitHandler       string
	CallHandler       string
	CheckpointHandler string
	RecoverHandler    string
	ShutdownHandler   string
	SignalHandler     string
}

// BuildHandlerMap function builder interface
type BuildHandlerMap interface {
	Handler() string
	HookHandler(runtime string, handlerInfo FunctionHookHandlerInfo) map[string]string
	ExtendedHandler(initializer, preStop string) map[string]string
	ExtendedTimeout(initializerTimeout, preStopTimeout int) map[string]int
}

// GetBuilder get builder
func GetBuilder(kind, runtime, handler string) BuildHandlerMap {
	switch kind {
	case Faas:
		return FaasBuilder{faasHandler: handler}
	case Yrlib:
		return YrlibBuilder{runtime: runtime}
	case PosixCustom:
		return PosixCustomBuilder{}
	case Custom:
		return CustomBuilder{}
	default:
		return nil
	}
}

// FaasBuilder builder for faas functions
type FaasBuilder struct {
	faasHandler string
}

// Handler get handler
func (f FaasBuilder) Handler() string {
	return f.faasHandler
}

// HookHandler get hook handler
func (f FaasBuilder) HookHandler(runtime string, handlerInfo FunctionHookHandlerInfo) map[string]string {
	hookHandler := map[string]string{}
	if strings.HasPrefix(runtime, PythonRuntimePrefix) {
		hookHandler = map[string]string{
			InitHandler:       "faas_executor.faasInitHandler",
			CallHandler:       "faas_executor.faasCallHandler",
			CheckpointHandler: "faas_executor.faasCheckPointHandler",
			RecoverHandler:    "faas_executor.faasRecoverHandler",
			ShutdownHandler:   "faas_executor.faasShutDownHandler",
			SignalHandler:     "faas_executor.faasSignalHandler",
		}
	} else if strings.HasPrefix(runtime, JavaRuntimePrefix) {
		hookHandler = map[string]string{
			InitHandler:       "com.huawei.faas.handler.FaaSExecutor.faasInitHandler",
			CallHandler:       "com.huawei.faas.handler.FaaSExecutor.faasCallHandler",
			CheckpointHandler: "com.huawei.faas.handler.FaaSExecutor.faasCheckPointHandler",
			RecoverHandler:    "com.huawei.faas.handler.FaaSExecutor.faasRecoverHandler",
			ShutdownHandler:   "com.huawei.faas.handler.FaaSExecutor.faasShutDownHandler",
			SignalHandler:     "com.huawei.faas.handler.FaaSExecutor.faasSignalHandler",
		}
	} else {
		fmt.Printf("faas: language matching error")
	}
	return hookHandler
}

// ExtendedHandler get extended handler
func (f FaasBuilder) ExtendedHandler(initializer, preStop string) map[string]string {
	extendedHandler := map[string]string{
		ExtendedInitializer: initializer,
		ExtendedPreStop:     preStop,
	}
	return cleanMap(extendedHandler)
}

// ExtendedTimeout get extended timeout
func (f FaasBuilder) ExtendedTimeout(initializerTimeout, preStopTimeout int) map[string]int {
	if initializerTimeout == 0 {
		return map[string]int{}
	}
	extendedHandler := map[string]int{
		ExtendedInitializer: initializerTimeout,
	}
	if preStopTimeout != 0 {
		extendedHandler[ExtendedPreStop] = preStopTimeout
	}
	return extendedHandler
}

// YrlibBuilder yrlib handler
type YrlibBuilder struct {
	runtime string
}

// ExtendedTimeout get extended timeout
func (y YrlibBuilder) ExtendedTimeout(initializerTimeout, preStopTimeout int) map[string]int {
	return map[string]int{}
}

// Handler get handler
func (y YrlibBuilder) Handler() string {
	if strings.HasPrefix(y.runtime, CppRuntimePrefix) {
		return ""
	}
	return yrlibHandler
}

// HookHandler get hook handler
func (y YrlibBuilder) HookHandler(runtime string, handlerInfo FunctionHookHandlerInfo) map[string]string {
	hookHandler := map[string]string{}
	if strings.HasPrefix(runtime, JavaRuntimePrefix) {
		hookHandler = map[string]string{
			InitHandler:       "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskInitHandler",
			CallHandler:       "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskCallHandler",
			CheckpointHandler: "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskCheckPointHandler",
			RecoverHandler:    "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskRecoverHandler",
			ShutdownHandler:   "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskShutdownHandler",
			SignalHandler:     "com.huawei.actortask.handler.ActorTaskExecutor.actorTaskSignalHandler",
		}
	} else {
		hookHandler = map[string]string{
			InitHandler:       y.getDefaultHandler(handlerInfo.InitHandler, "yrlib_handler.init"),
			CallHandler:       y.getDefaultHandler(handlerInfo.CallHandler, "yrlib_handler.call"),
			CheckpointHandler: y.getDefaultHandler(handlerInfo.CheckpointHandler, "yrlib_handler.checkpoint"),
			RecoverHandler:    y.getDefaultHandler(handlerInfo.RecoverHandler, "yrlib_handler.recover"),
			ShutdownHandler:   y.getDefaultHandler(handlerInfo.ShutdownHandler, "yrlib_handler.shutdown"),
			SignalHandler:     y.getDefaultHandler(handlerInfo.SignalHandler, "yrlib_handler.signal"),
		}
	}
	return cleanMap(hookHandler)
}

func (y YrlibBuilder) getDefaultHandler(handler, defaultHandler string) string {
	if strings.HasPrefix(y.runtime, CppRuntimePrefix) {
		return handler
	}
	if handler == "" {
		return defaultHandler
	}
	return handler
}

// ExtendedHandler get extended handler
func (y YrlibBuilder) ExtendedHandler(initializer, preStop string) map[string]string {
	return map[string]string{}
}

// PosixCustomBuilder builder for posix custom
type PosixCustomBuilder struct{}

// ExtendedTimeout get extended timeout
func (p PosixCustomBuilder) ExtendedTimeout(initializerTimeout, preStopTimeout int) map[string]int {
	return map[string]int{}
}

// Handler get handler
func (p PosixCustomBuilder) Handler() string {
	return customHandler
}

// HookHandler get hook handler
func (p PosixCustomBuilder) HookHandler(runtime string, handlerInfo FunctionHookHandlerInfo) map[string]string {
	return map[string]string{}
}

// ExtendedHandler get extended handler
func (p PosixCustomBuilder) ExtendedHandler(initializer, preStop string) map[string]string {
	return map[string]string{}
}
func cleanMap(handlerMap map[string]string) map[string]string {
	for key, value := range handlerMap {
		if value == "" {
			delete(handlerMap, key)
		}
	}
	return handlerMap
}

// CustomBuilder builder for custom function
type CustomBuilder struct{}

// ExtendedTimeout get extended timeout
func (c CustomBuilder) ExtendedTimeout(initializerTimeout, preStopTimeout int) map[string]int {
	return map[string]int{}
}

// Handler get handler
func (c CustomBuilder) Handler() string {
	return ""
}

// HookHandler get hook handler
func (c CustomBuilder) HookHandler(runtime string, handlerInfo FunctionHookHandlerInfo) map[string]string {
	hookHandler := map[string]string{
		InitHandler:       handlerInfo.InitHandler,
		CallHandler:       handlerInfo.CallHandler,
		CheckpointHandler: handlerInfo.CheckpointHandler,
		RecoverHandler:    handlerInfo.RecoverHandler,
		ShutdownHandler:   handlerInfo.ShutdownHandler,
		SignalHandler:     handlerInfo.SignalHandler,
	}
	return cleanMap(hookHandler)
}

// ExtendedHandler get extended handler
func (c CustomBuilder) ExtendedHandler(initializer, preStop string) map[string]string {
	return map[string]string{}
}
