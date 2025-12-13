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
	"os"
	"strings"
	"testing"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"

	"cli/constant"
	"cli/utils"
)

func TestClusterCtrl(t *testing.T) {
	Convey("Test ClusterCtrl functions", t, func() {
		// 测试 Up 函数
		Convey("Up", func() {
			Convey("Given a valid cluster config and successful executor", func() {
				cfg := &utils.ClusterConfig{
					MasterIp:  []string{"master-ip"},
					AgentIp:   []string{"agent1-ip", "agent2-ip"},
					MasterCmd: []string{"start-master"},
					AgentCmd:  []string{"start-agent " + utils.PlaceholderMasterInfo},
				}
				masterInfo := "master-info-data"
				mock := &mockExecutor{}
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
					if host == "master-ip" && cmds[0] == "start-master" {
						return nil, nil
					}
					return nil, errors.New("unexpected command")
				})
				patches.ApplyMethod(mock, "RunCommandSync", func(_ *mockExecutor, host, cmd string) ([]byte, error) {
					if host == "master-ip" && cmd == fmt.Sprintf("cat %s", constant.DefaultYuanRongCurrentMasterInfoPath) {
						return []byte(masterInfo + "\n"), nil
					}
					return nil, errors.New("unexpected command")
				})
				patches.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
					if len(hosts) == 2 && hosts[0] == "agent1-ip" && hosts[1] == "agent2-ip" && cmds[0] == "start-agent "+masterInfo {
						return []result{{Host: "agent1-ip"}, {Host: "agent2-ip"}}, nil
					}
					return nil, errors.New("unexpected hosts or commands")
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

				Convey("When Up is called", func() {
					err := Up(cfg, mock)

					Convey("Then it should start the cluster successfully", func() {
						So(err, ShouldBeNil)

						// 读取捕获的输出
						err := w.Close()
						if err != nil {
							return
						}
						var buf strings.Builder
						_, _ = io.Copy(&buf, r)
						output := buf.String()
						So(output, ShouldContainSubstring, "succeed to start up master node, master info: "+masterInfo)
						So(output, ShouldContainSubstring, "succeed to start up 2 agent nodes")
						So(output, ShouldContainSubstring, "succeed to up a yuanrong cluster")
					})
				})
			})

			Convey("Given a config with invalid master IP count", func() {
				cfg := &utils.ClusterConfig{
					MasterIp: []string{"master1-ip", "master2-ip"}, // 多个 master IP
				}
				mock := &mockExecutor{}

				Convey("When Up is called", func() {
					err := Up(cfg, mock)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "only accept one master ip")
					})
				})
			})

			Convey("Given a failure in upMasterNode", func() {
				cfg := &utils.ClusterConfig{
					MasterIp:  []string{"master-ip"},
					MasterCmd: []string{"start-master"},
				}
				mock := &mockExecutor{}
				patches := gomonkey.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
					return nil, errors.New("master startup failed")
				})
				defer patches.Reset()

				Convey("When Up is called", func() {
					err := Up(cfg, mock)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "master startup failed")
					})
				})
			})
		})

		// 测试 upMasterNode
		Convey("upMasterNode", func() {
			Convey("Given a valid config with one master IP", func() {
				cfg := &utils.ClusterConfig{
					MasterIp:  []string{"master-ip"},
					MasterCmd: []string{"start-master"},
				}
				masterInfo := "master-info-data"
				mock := &mockExecutor{}
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
					return nil, nil
				})
				patches.ApplyMethod(mock, "RunCommandSync", func(_ *mockExecutor, host, cmd string) ([]byte, error) {
					if cmd == fmt.Sprintf("cat %s", constant.DefaultYuanRongCurrentMasterInfoPath) {
						return []byte(masterInfo + "\n"), nil
					}
					return nil, errors.New("unexpected command")
				})
				defer patches.Reset()

				Convey("When upMasterNode is called", func() {
					result, err := upMasterNode(cfg, mock)

					Convey("Then it should return master info without error", func() {
						So(err, ShouldBeNil)
						So(result, ShouldEqual, masterInfo)
					})
				})
			})

			Convey("Given a config with multiple master IPs", func() {
				cfg := &utils.ClusterConfig{
					MasterIp: []string{"master1-ip", "master2-ip"},
				}
				mock := &mockExecutor{}

				Convey("When upMasterNode is called", func() {
					result, err := upMasterNode(cfg, mock)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "only accept one master ip")
						So(result, ShouldEqual, "")
					})
				})
			})

			Convey("Given a failure to get master info", func() {
				cfg := &utils.ClusterConfig{
					MasterIp:  []string{"master-ip"},
					MasterCmd: []string{"start-master"},
				}
				mock := &mockExecutor{}
				patches := gomonkey.NewPatches()
				patches.ApplyMethod(mock, "RunCommandsSync", func(_ *mockExecutor, host string, cmds []string) ([]byte, error) {
					return nil, nil
				})
				patches.ApplyMethod(mock, "RunCommandSync", func(_ *mockExecutor, host, cmd string) ([]byte, error) {
					return nil, errors.New("cat failed")
				})
				defer patches.Reset()

				Convey("When upMasterNode is called", func() {
					result, err := upMasterNode(cfg, mock)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldContainSubstring, "failed to get master info")
						So(result, ShouldEqual, "")
					})
				})
			})
		})

		// 测试 upAgentNode
		Convey("upAgentNode", func() {
			Convey("Given a valid config and master info", func() {
				cfg := &utils.ClusterConfig{
					AgentIp:  []string{"agent1-ip", "agent2-ip"},
					AgentCmd: []string{"start-agent " + utils.PlaceholderMasterInfo},
				}
				masterInfo := "master-info-data"
				mock := &mockExecutor{}
				patches := gomonkey.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
					if len(hosts) == 2 && hosts[0] == "agent1-ip" && hosts[1] == "agent2-ip" && cmds[0] == "start-agent "+masterInfo {
						return []result{{Host: "agent1-ip"}, {Host: "agent2-ip"}}, nil
					}
					return nil, errors.New("unexpected hosts or commands")
				})
				defer patches.Reset()

				Convey("When upAgentNode is called", func() {
					err := upAgentNode(cfg, mock, masterInfo)

					Convey("Then it should start agent nodes without error", func() {
						So(err, ShouldBeNil)
					})
				})
			})

			Convey("Given a failure in agent node startup", func() {
				cfg := &utils.ClusterConfig{
					AgentIp:  []string{"agent1-ip"},
					AgentCmd: []string{"start-agent " + utils.PlaceholderMasterInfo},
				}
				masterInfo := "master-info-data"
				mock := &mockExecutor{}
				patches := gomonkey.ApplyMethod(mock, "RunCommandsOnHostsParallelSync", func(_ *mockExecutor, hosts []string, cmds []string) ([]result, error) {
					return nil, errors.New("agent startup failed")
				})
				defer patches.Reset()

				Convey("When upAgentNode is called", func() {
					err := upAgentNode(cfg, mock, masterInfo)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "agent startup failed")
					})
				})
			})
		})
	})
}
