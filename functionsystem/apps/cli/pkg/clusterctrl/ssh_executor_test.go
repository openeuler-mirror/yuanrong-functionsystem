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
	"io"
	"os/exec"
	"strings"
	"testing"

	"cli/utils"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"
)

func TestSSHRemoteExecutor(t *testing.T) {
	// mockExecutor 定义假定已在 remote_executor_test.go 中，这里创建临时实例
	dummyMockExecutor := &mockExecutor{} // 用于 mockReadCloser 的 closer 字段

	Convey("Test SSHRemoteExecutor", t, func() {
		// 测试 prepareCommand
		Convey("prepareCommand", func() {
			Convey("Given a valid SSH command template", func() {
				template := "ssh -o ConnectTimeout=5 " + utils.PlaceholderTarget + " " + utils.PlaceholderCommand
				se := &SSHRemoteExecutor{sshCmdTemplate: template}

				Convey("When prepareCommand is called with host and command", func() {
					cmd := se.prepareCommand("test-host", "echo hello")

					Convey("Then it should construct the correct command", func() {
						So(len(cmd.Args), ShouldEqual, 6)
						So(cmd.Args[0], ShouldEqual, "ssh")
						So(cmd.Args[1], ShouldEqual, "-o")
						So(cmd.Args[2], ShouldEqual, "ConnectTimeout=5")
						So(cmd.Args[3], ShouldEqual, "test-host")
						So(cmd.Args[4], ShouldEqual, "echo")
						So(cmd.Args[5], ShouldEqual, "hello")
					})
				})
			})
		})

		// 测试 RunCommandWithPipe
		Convey("RunCommandWithPipe", func() {
			Convey("Given a successful SSH command execution", func() {
				template := "ssh " + utils.PlaceholderTarget + " " + utils.PlaceholderCommand
				se := NewSSHRemoteExecutor(template).(*SSHRemoteExecutor)
				output := "hello from remote\n"
				mockPipe := &mockReadCloser{Reader: strings.NewReader(output), closer: dummyMockExecutor}

				// 创建真实的 *exec.Cmd
				cmd := exec.Command("ssh", "test-host", "echo hello")
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(cmd, "StdoutPipe", func(*exec.Cmd) (io.ReadCloser, error) {
					return mockPipe, nil
				})
				patches.ApplyMethod(cmd, "Start", func(*exec.Cmd) error {
					return nil
				})
				patches.ApplyMethod(cmd, "Wait", func(*exec.Cmd) error {
					return nil
				})
				patches.ApplyFunc(exec.Command, func(name string, args ...string) *exec.Cmd {
					return cmd
				})
				defer patches.Reset()

				Convey("When RunCommandWithPipe is called", func() {
					reader, waitFn, err := se.RunCommandWithPipe("test-host", "echo hello", []string{})

					Convey("Then it should return a valid reader and wait function without error", func() {
						So(err, ShouldBeNil)
						So(reader, ShouldNotBeNil)
						So(waitFn, ShouldNotBeNil)

						// 读取输出
						buf := make([]byte, 1024)
						n, readErr := reader.Read(buf)
						So(readErr, ShouldBeNil)
						So(string(buf[:n]), ShouldEqual, "hello from remote\n")

						// 测试 waitFn
						waitErr := waitFn()
						So(waitErr, ShouldBeNil)

						// 关闭 reader
						closeErr := reader.Close()
						So(closeErr, ShouldBeNil)
					})
				})
			})

			Convey("Given an SSH command with stdout pipe error", func() {
				template := "ssh " + utils.PlaceholderTarget + " " + utils.PlaceholderCommand
				se := NewSSHRemoteExecutor(template).(*SSHRemoteExecutor)

				cmd := exec.Command("ssh", "test-host", "echo hello")
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(cmd, "StdoutPipe", func(*exec.Cmd) (io.ReadCloser, error) {
					return nil, errors.New("stdout pipe not available")
				})
				patches.ApplyFunc(exec.Command, func(name string, args ...string) *exec.Cmd {
					return cmd
				})
				defer patches.Reset()

				Convey("When RunCommandWithPipe is called", func() {
					reader, waitFn, err := se.RunCommandWithPipe("test-host", "echo hello", []string{})

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldContainSubstring, "failed to get stdout pipe")
						So(reader, ShouldBeNil)
						So(waitFn, ShouldBeNil)
					})
				})
			})

			Convey("Given an SSH command that fails to start", func() {
				template := "ssh " + utils.PlaceholderTarget + " " + utils.PlaceholderCommand
				se := NewSSHRemoteExecutor(template).(*SSHRemoteExecutor)
				mockPipe := &mockReadCloser{Reader: strings.NewReader(""), closer: dummyMockExecutor}

				cmd := exec.Command("ssh", "test-host", "echo hello")
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(cmd, "StdoutPipe", func(*exec.Cmd) (io.ReadCloser, error) {
					return mockPipe, nil
				})
				patches.ApplyMethod(cmd, "Start", func(*exec.Cmd) error {
					return errors.New("ssh connection failed")
				})
				patches.ApplyFunc(exec.Command, func(name string, args ...string) *exec.Cmd {
					return cmd
				})
				defer patches.Reset()

				Convey("When RunCommandWithPipe is called", func() {
					reader, waitFn, err := se.RunCommandWithPipe("test-host", "echo hello", []string{})

					Convey("Then it should return an error and close the pipe", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldContainSubstring, "failed to start command")
						So(reader, ShouldBeNil)
						So(waitFn, ShouldBeNil)
						So(mockPipe.closer.closed, ShouldBeTrue)
					})
				})
			})

			Convey("Given an SSH command that fails on wait", func() {
				template := "ssh " + utils.PlaceholderTarget + " " + utils.PlaceholderCommand
				se := NewSSHRemoteExecutor(template).(*SSHRemoteExecutor)
				output := "partial output\n"
				mockPipe := &mockReadCloser{Reader: strings.NewReader(output), closer: dummyMockExecutor}

				cmd := exec.Command("ssh", "test-host", "fail command")
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(cmd, "StdoutPipe", func(*exec.Cmd) (io.ReadCloser, error) {
					return mockPipe, nil
				})
				patches.ApplyMethod(cmd, "Start", func(*exec.Cmd) error {
					return nil
				})
				patches.ApplyMethod(cmd, "Wait", func(*exec.Cmd) error {
					return errors.New("command exited with error")
				})
				patches.ApplyFunc(exec.Command, func(name string, args ...string) *exec.Cmd {
					return cmd
				})
				defer patches.Reset()

				Convey("When RunCommandWithPipe is called", func() {
					reader, waitFn, err := se.RunCommandWithPipe("test-host", "fail command", []string{})

					Convey("Then it should return a reader and a wait function with error", func() {
						So(err, ShouldBeNil)
						So(reader, ShouldNotBeNil)
						So(waitFn, ShouldNotBeNil)

						// 读取输出
						buf := make([]byte, 1024)
						n, readErr := reader.Read(buf)
						So(readErr, ShouldBeNil)
						So(string(buf[:n]), ShouldEqual, "partial output\n")

						// 测试 waitFn
						waitErr := waitFn()
						So(waitErr, ShouldResemble, errors.New("command exited with error"))
					})
				})
			})
		})
	})
}
