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

// Package clusterctrl provides the clusterctrl commands, includes Up, Down, Exec
// and some RemoteExecutor (SSHRemoteExecutor)
package clusterctrl

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"strings"
	"sync"
)

// RemoteExecutor interface
type RemoteExecutor interface {
	// RunCommandWithPipe implement a basic method
	RunCommandWithPipe(_ string, _ string, _ []string) (io.ReadCloser, func() error, error)
	// RunCommandSync run a single command on a remote host
	RunCommandSync(host string, command string, _ []string) ([]byte, error)
	// RunCommandsSync - execute multiple commands at once
	RunCommandsSync(host string, commands []string, _ []string) ([]byte, error)
	// RunCommandsOnHostsParallelSync - execute multiple commands at multiple hosts sync
	RunCommandsOnHostsParallelSync(hosts []string, commands []string, _ []string) ([]result, error)
	// RunCommandStream - the result would be like a chunk
	RunCommandStream(host string, command string, _ []string) (chan result, error)
}

type result struct {
	Host   string
	Output []byte
	Error  error
}

type baseRemoteExecutor struct {
	// Impl is the real implement of the RunCommandWithPipe method
	Impl RemoteExecutor
}

// RunCommandWithPipe - basic method, should be implemented by real implementation
// all other methods will be implemented with this basic method
func (b *baseRemoteExecutor) RunCommandWithPipe(_ string, _ string, _ []string) (io.ReadCloser, func() error, error) {
	return nil, nil, errors.New("the RunCommandWithPipe is NOT IMPLEMENTED")
}

// RunCommandSync run a single command on a remote host
func (b *baseRemoteExecutor) RunCommandSync(host string, command string, env []string) ([]byte, error) {
	outputPipe, waitFn, err := b.Impl.RunCommandWithPipe(host, command, env)
	if err != nil {
		return nil, err
	}
	defer func(outputPipe io.ReadCloser) {
		if outputPipe.Close() != nil {
			return
		}
	}(outputPipe)

	var output bytes.Buffer
	_, err = io.Copy(&output, outputPipe)
	if err != nil {
		return nil, fmt.Errorf("failed to read command output: %w", err)
	}
	if err := waitFn(); err != nil {
		return output.Bytes(), fmt.Errorf("command failed: %w, output: %s", err, output.Bytes())
	}
	return output.Bytes(), nil
}

// RunCommandsSync - execute multiple commands at once
func (b *baseRemoteExecutor) RunCommandsSync(host string, commands []string, env []string) ([]byte, error) {
	script := strings.Join(commands, " && ")
	return b.Impl.RunCommandSync(host, script, env)
}

// RunCommandsOnHostsParallelSync - execute multiple commands at multiple hosts sync
func (b *baseRemoteExecutor) RunCommandsOnHostsParallelSync(hosts []string, commands []string, env []string) ([]result, error) {
	var wg sync.WaitGroup
	var errs []error
	results := make([]result, len(hosts))
	var mu sync.Mutex // protect results and errs

	wg.Add(len(hosts))
	for i, host := range hosts {
		go func(i int, host string) {
			defer wg.Done()
			output, err := b.Impl.RunCommandsSync(host, commands, env)
			results[i] = result{Host: host, Output: output, Error: err}
			if err != nil {
				mu.Lock()
				errs = append(errs, fmt.Errorf("host %s, command %s: %w", host, strings.Join(commands, ","), err))
				mu.Unlock()
			}
		}(i, host)
	}
	wg.Wait()

	if len(errs) > 0 {
		return results, errors.Join(errs...)
	}
	return results, nil
}

// RunCommandStream execute a method and redirect the output as a stream
func (b *baseRemoteExecutor) RunCommandStream(host string, command string, env []string) (chan result, error) {
	pipe, waitFn, err := b.Impl.RunCommandWithPipe(host, command, env)
	if err != nil {
		return nil, err
	}

	outCh := make(chan result, 100)
	go func() {
		defer close(outCh)
		defer func(pipe io.ReadCloser) {
			if pipe.Close() != nil {
			}
		}(pipe)

		buf := make([]byte, 1024)
		for {
			n, err := pipe.Read(buf)
			if n > 0 {
				outCh <- result{Output: append([]byte(nil), buf[:n]...)}
			}
			if err != nil {
				waitErr := waitFn()
				outCh <- result{Error: waitErr}
				return
			}
		}
	}()

	return outCh, nil
}
