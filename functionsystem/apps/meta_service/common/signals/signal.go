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

// Package signals is for shutdownHandler
package signals

import (
	"os"
	"os/signal"
	"syscall"
)

var (
	shutdownSignals      = []os.Signal{os.Interrupt, syscall.SIGTERM, syscall.SIGKILL}
	onlyOneSignalHandler = make(chan struct{})
	shutdownHandler      chan os.Signal
)

const channelCount = 2

// WaitForSignal defines signal handler process.
func WaitForSignal() <-chan struct{} {
	close(onlyOneSignalHandler) // panic when called twice

	// 2 is the length of shutdown Handler channel
	shutdownHandler = make(chan os.Signal, channelCount)

	stopCh := make(chan struct{})
	signal.Notify(shutdownHandler, shutdownSignals...)

	go func() {
		<-shutdownHandler
		close(stopCh)
		<-shutdownHandler
		os.Exit(1)
	}()

	return stopCh
}
