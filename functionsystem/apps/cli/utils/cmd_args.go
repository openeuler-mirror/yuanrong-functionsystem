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

package utils

import (
	"context"
	"fmt"
	"os/exec"
	"runtime"
	"time"

	"github.com/spf13/cobra"
)

// NoArgs 无参数处理
func NoArgs(cmd *cobra.Command, args []string) error {
	if len(args) < 1 {
		return nil
	}
	// 打印未知参数
	return fmt.Errorf("unknown argument %q", args)
}

// GetGOOSType 返回当前系统应该使用的启动命令
func GetGOOSType() (string, string) {
	if runtime.GOOS == "windows" {
		return "CMD", "/C"
	}
	return "/bin/bash", "-c"
}

// ExecCommandUntil - this function will also check if the command exit, so it will return the channel with wait called
func ExecCommandUntil(cmd *exec.Cmd, stopCond func(ctx context.Context, block bool) error, timeout int, block bool) (
	error, chan error,
) {
	if err := cmd.Start(); err != nil {
		fmt.Println("failed to start sub command:", err)
		return err, nil
	}
	var ctx context.Context
	var cancel context.CancelFunc
	if !block && timeout > 0 {
		ctx, cancel = context.WithTimeout(context.Background(), time.Duration(timeout)*time.Second)
	} else {
		ctx, cancel = context.WithCancel(context.Background())
	}
	defer cancel()

	// Create a channel to receive the result of stopCond
	resultChan := make(chan error, 1)
	go func() {
		resultChan <- stopCond(ctx, block)
	}()

	cmdExitChan := make(chan error, 1)
	go func() {
		cmdExitChan <- cmd.Wait()
	}()

	select {
	case err := <-resultChan:
		return err, cmdExitChan
	case err := <-cmdExitChan:
		return err, cmdExitChan
	case <-ctx.Done():
		return fmt.Errorf("timeout(%ds) reached", timeout), cmdExitChan
	}
}
