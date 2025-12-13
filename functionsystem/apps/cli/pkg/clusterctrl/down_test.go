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

func TestExecAndDown(t *testing.T) {
	// 测试 Down 函数
	Convey("Down", t, func() {
		Convey("Given a valid config and successful shutdown", func() {
			cfg := &utils.ClusterConfig{
				MasterIp:    []string{"master-ip"},
				AgentIp:     []string{"agent1-ip", "agent2-ip"},
				StopCmd:     []string{"stop " + utils.PlaceholderClusterName},
				ClusterName: "test-cluster",
			}
			mock := &mockExecutor{}
			patches := gomonkey.NewPatches()
			patches.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
				if len(hosts) == 2 && hosts[0] == "agent1-ip" && hosts[1] == "agent2-ip" && cmds[0] == "stop test-cluster" {
					return []result{{Host: "agent1-ip"}, {Host: "agent2-ip"}}, nil
				}
				return nil, errors.New("unexpected agent hosts or commands")
			})
			patches.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
				if host == "master-ip" && cmds[0] == "stop test-cluster" {
					return nil, nil
				}
				return nil, errors.New("unexpected master host or command")
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

			Convey("When Down is called", func() {
				err := Down(cfg, mock)

				Convey("Then it should stop the cluster successfully", func() {
					So(err, ShouldBeNil)

					// 读取捕获的输出
					err := w.Close()
					if err != nil {
						return
					}
					var buf strings.Builder
					_, _ = io.Copy(&buf, r)
					output := buf.String()
					So(output, ShouldContainSubstring, "succeed to stop all agents")
					So(output, ShouldContainSubstring, "succeed to stop master")
				})
			})
		})

		Convey("Given a failure in stopping agents", func() {
			cfg := &utils.ClusterConfig{
				MasterIp:    []string{"master-ip"},
				AgentIp:     []string{"agent1-ip"},
				StopCmd:     []string{"stop " + utils.PlaceholderClusterName},
				ClusterName: "test-cluster",
			}
			mock := &mockExecutor{}
			patches := gomonkey.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
				return nil, errors.New("agent shutdown failed")
			})
			defer patches.Reset()

			Convey("When Down is called", func() {
				err := Down(cfg, mock)

				Convey("Then it should return an error", func() {
					So(err, ShouldNotBeNil)
					So(err.Error(), ShouldEqual, "agent shutdown failed")
				})
			})
		})

		Convey("Given a failure in stopping master", func() {
			cfg := &utils.ClusterConfig{
				MasterIp:    []string{"master-ip"},
				AgentIp:     []string{"agent1-ip"},
				StopCmd:     []string{"stop " + utils.PlaceholderClusterName},
				ClusterName: "test-cluster",
			}
			mock := &mockExecutor{}
			patches := gomonkey.NewPatches()
			patches.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
				return []result{{Host: "agent1-ip"}}, nil
			})
			patches.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
				return nil, errors.New("master shutdown failed")
			})
			defer patches.Reset()

			Convey("When Down is called", func() {
				err := Down(cfg, mock)

				Convey("Then it should return an error", func() {
					So(err, ShouldNotBeNil)
					So(err.Error(), ShouldEqual, "master shutdown failed")
				})
			})
		})
	})
}
