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

package clusterctrl

import (
	"errors"
	"fmt"
	"io"
	"strings"
	"sync"
	"testing"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"
)

// mockExecutor 是 RemoteExecutor 的 mock 实现
type mockExecutor struct {
	runPipeOutput string
	runPipeError  error
	waitFnError   error
	mutex         sync.Mutex
	closed        bool
}

func newMockExecutor(output string, runErr, waitErr error) *mockExecutor {
	return &mockExecutor{
		runPipeOutput: output,
		runPipeError:  runErr,
		waitFnError:   waitErr,
	}
}

func (m *mockExecutor) RunCommandWithPipe(_, _ string, _ []string) (io.ReadCloser, func() error, error) {
	if m.runPipeError != nil {
		return nil, nil, m.runPipeError
	}
	reader := strings.NewReader(m.runPipeOutput)
	rc := &mockReadCloser{Reader: reader, closer: m}
	waitFn := func() error {
		return m.waitFnError
	}
	return rc, waitFn, nil
}

func (m *mockExecutor) RunCommandSync(_, _ string, _ []string) ([]byte, error) {
	return nil, errors.New("not implemented")
}

func (m *mockExecutor) RunCommandsSync(_ string, _ []string, _ []string) ([]byte, error) {
	return nil, errors.New("not implemented")
}

func (m *mockExecutor) RunCommandsOnHostsParallelSync(_ []string, _ []string, _ []string) ([]result, error) {
	return nil, errors.New("not implemented")
}

func (m *mockExecutor) RunCommandStream(_, _ string, _ []string) (chan result, error) {
	return nil, errors.New("not implemented")
}

type mockReadCloser struct {
	io.Reader
	closer *mockExecutor
}

func (m *mockReadCloser) Close() error {
	m.closer.mutex.Lock()
	defer m.closer.mutex.Unlock()
	if !m.closer.closed {
		m.closer.closed = true
	}
	return nil
}

// 测试文件
func TestBaseRemoteExecutor(t *testing.T) {
	Convey("Test baseRemoteExecutor", t, func() {
		// 测试 RunCommandSync
		Convey("RunCommandSync", func() {
			Convey("Given a successful command execution", func() {
				mock := newMockExecutor("hello\nworld\n", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}

				Convey("When RunCommandSync is called", func() {
					output, err := b.RunCommandSync("test-host", "echo hello", []string{})

					Convey("Then it should return the output without error", func() {
						So(err, ShouldBeNil)
						So(string(output), ShouldEqual, "hello\nworld\n")
					})
				})
			})

			Convey("Given a command execution with wait error", func() {
				mock := newMockExecutor("partial output", nil, errors.New("command failed"))
				b := &baseRemoteExecutor{Impl: mock}

				Convey("When RunCommandSync is called", func() {
					output, err := b.RunCommandSync("test-host", "fail", []string{})

					Convey("Then it should return partial output with error", func() {
						So(err, ShouldNotBeNil)
						So(string(output), ShouldEqual, "partial output")
						So(err.Error(), ShouldContainSubstring, "command failed")
					})
				})
			})

			Convey("Given a command execution with initial error", func() {
				mock := newMockExecutor("", errors.New("connection failed"), nil)
				b := &baseRemoteExecutor{Impl: mock}

				Convey("When RunCommandSync is called", func() {
					output, err := b.RunCommandSync("test-host", "test", []string{})

					Convey("Then it should return error without output", func() {
						So(err, ShouldResemble, errors.New("connection failed"))
						So(output, ShouldBeNil)
					})
				})
			})
		})

		// 测试 RunCommandsSync
		Convey("RunCommandsSync", func() {
			Convey("Given multiple successful commands", func() {
				mock := newMockExecutor("hello\nworld\n", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}
				patches := gomonkey.ApplyMethodFunc(mock, "RunCommandSync",
					func(host, cmd string) ([]byte, error) {
						return []byte("hello\nworld\n"), nil
					})
				defer patches.Reset()

				Convey("When RunCommandsSync is called", func() {
					output, err := b.RunCommandsSync("test-host", []string{"echo hello", "echo world"}, []string{})

					Convey("Then it should return concatenated output", func() {
						So(err, ShouldBeNil)
						So(string(output), ShouldEqual, "hello\nworld\n")
					})
				})
			})

			Convey("Given multiple commands with error", func() {
				mock := newMockExecutor("", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}
				patches := gomonkey.ApplyMethodFunc(mock, "RunCommandSync",
					func(host, cmd string) ([]byte, error) {
						return nil, errors.New("execution failed")
					})
				defer patches.Reset()

				Convey("When RunCommandsSync is called", func() {
					output, err := b.RunCommandsSync("test-host", []string{"fail"}, []string{})

					Convey("Then it should return error", func() {
						So(err, ShouldNotBeNil)
						So(output, ShouldBeNil)
					})
				})
			})
		})

		// 测试 RunCommandsOnHostsParallelSync
		Convey("RunCommandsOnHostsParallelSync", func() {
			Convey("Given multiple hosts with successful commands", func() {
				mock := newMockExecutor("", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}
				patches := gomonkey.ApplyMethodFunc(mock, "RunCommandsSync",
					func(host string, _ []string) ([]byte, error) {
						return []byte(fmt.Sprintf("test from %s", host)), nil
					})
				defer patches.Reset()

				Convey("When RunCommandsOnHostsParallelSync is called", func() {
					results, err := b.RunCommandsOnHostsParallelSync([]string{"host1", "host2"}, []string{"echo test"}, []string{})

					Convey("Then it should return results for all hosts", func() {
						So(err, ShouldBeNil)
						So(len(results), ShouldEqual, 2)
						So(string(results[0].Output), ShouldEqual, "test from host1")
						So(string(results[1].Output), ShouldEqual, "test from host2")
						So(results[0].Error, ShouldBeNil)
						So(results[1].Error, ShouldBeNil)
					})
				})
			})

			Convey("Given multiple hosts with one failure", func() {
				mock := newMockExecutor("", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}
				patches := gomonkey.ApplyMethodFunc(mock, "RunCommandsSync",
					func(host string, _ []string) ([]byte, error) {
						if host == "host2" {
							return nil, errors.New("failed on host2")
						}
						return []byte("test from host1"), nil
					})
				defer patches.Reset()

				Convey("When RunCommandsOnHostsParallelSync is called", func() {
					results, err := b.RunCommandsOnHostsParallelSync([]string{"host1", "host2"}, []string{"echo test"}, []string{})

					Convey("Then it should return partial results with error", func() {
						So(err, ShouldNotBeNil)
						So(len(results), ShouldEqual, 2)
						So(string(results[0].Output), ShouldEqual, "test from host1")
						So(results[0].Error, ShouldBeNil)
						So(results[1].Error, ShouldNotBeNil)
					})
				})
			})
		})

		// 测试 RunCommandStream
		Convey("RunCommandStream", func() {
			Convey("Given a streaming command", func() {
				mock := newMockExecutor("chunk1\nchunk2\n", nil, nil)
				b := &baseRemoteExecutor{Impl: mock}

				Convey("When RunCommandStream is called", func() {
					ch, err := b.RunCommandStream("test-host", "stream data", []string{})

					Convey("Then it should stream the output in chunks", func() {
						So(err, ShouldBeNil)
						var outputs []string
						for r := range ch {
							So(r.Error, ShouldBeNil)
							outputs = append(outputs, string(r.Output))
						}
						So(len(outputs), ShouldBeGreaterThan, 0)
						So(strings.Join(outputs, ""), ShouldEqual, "chunk1\nchunk2\n")
					})
				})
			})

			Convey("Given a streaming command with error", func() {
				mock := newMockExecutor("partial", nil, errors.New("stream failed"))
				b := &baseRemoteExecutor{Impl: mock}

				Convey("When RunCommandStream is called", func() {
					ch, err := b.RunCommandStream("test-host", "fail stream", []string{})

					Convey("Then it should return partial output and an error", func() {
						So(err, ShouldBeNil)
						var outputs []string
						var lastErr error
						for r := range ch {
							if r.Error != nil {
								lastErr = r.Error
							} else {
								outputs = append(outputs, string(r.Output))
							}
						}
						So(len(outputs), ShouldEqual, 1)
						So(outputs[0], ShouldEqual, "partial")
						So(lastErr, ShouldResemble, errors.New("stream failed"))
					})
				})
			})
		})
	})
}
