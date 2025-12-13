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
	"os"
	"strings"
	"testing"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"

	"cli/utils"
)

func TestExec(t *testing.T) {
	// 测试 Exec 函数
	Convey("Exec", t, func() {
		Convey("Given a valid config and successful command stream", func() {
			cfg := &utils.ClusterConfig{
				MasterIp: []string{"master-ip"},
			}
			execCommand := "echo hello"
			mock := &mockExecutor{}
			output := []string{"hello\n", "world\n"}
			streamChan := make(chan result, 2)
			for _, out := range output {
				streamChan <- result{Output: []byte(out)}
			}
			close(streamChan)
			patches := gomonkey.ApplyMethod(mock, "RunCommandStream", func(_ *mockExecutor, host, cmd string) (chan result, error) {
				if host == "master-ip" && cmd == execCommand {
					return streamChan, nil
				}
				return nil, errors.New("unexpected host or command")
			})
			defer patches.Reset()

			// 重定向 stdout 以捕获输出
			origStdout := os.Stdout
			r, w, _ := os.Pipe()
			os.Stdout = w
			defer func() {
				err := w.Close()
				if err != nil {
					return
				}
				os.Stdout = origStdout
			}()

			Convey("When Exec is called", func() {
				err := Exec(cfg, mock, execCommand)

				Convey("Then it should stream the output without error", func() {
					So(err, ShouldBeNil)

					err := w.Close()
					if err != nil {
						return
					}
					var buf strings.Builder
					_, _ = io.Copy(&buf, r)
					outputStr := buf.String()
					So(outputStr, ShouldEqual, "hello\nworld\n.")
				})
			})
		})

		Convey("Given a config with multiple master IPs", func() {
			cfg := &utils.ClusterConfig{
				MasterIp: []string{"master1-ip", "master2-ip"},
			}
			mock := &mockExecutor{}

			Convey("When Exec is called", func() {
				err := Exec(cfg, mock, "echo hello")

				Convey("Then it should return an error", func() {
					So(err, ShouldNotBeNil)
					So(err.Error(), ShouldEqual, "only accept one master ip")
				})
			})
		})

		Convey("Given a failure in command stream", func() {
			cfg := &utils.ClusterConfig{
				MasterIp: []string{"master-ip"},
			}
			execCommand := "fail command"
			mock := &mockExecutor{}
			patches := gomonkey.ApplyMethod(mock, "RunCommandStream", func(_ *mockExecutor, host, cmd string) (chan result, error) {
				return nil, errors.New("stream failed")
			})
			defer patches.Reset()

			Convey("When Exec is called", func() {
				err := Exec(cfg, mock, execCommand)

				Convey("Then it should return an error", func() {
					So(err, ShouldNotBeNil)
					So(err.Error(), ShouldEqual, "stream failed")
				})
			})
		})
	})
}
