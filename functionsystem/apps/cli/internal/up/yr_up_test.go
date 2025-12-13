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

package up

import (
	"errors"
	"testing"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/spf13/cobra"

	"cli/pkg/clusterctrl"
	"cli/pkg/cmdio"
	"cli/utils"
)

func TestUpCmd(t *testing.T) {
	Convey("Test Up Command", t, func() {
		// 测试 InitCMD
		Convey("InitCMD", func() {
			Convey("Given a CmdIO instance", func() {
				cio := &cmdio.CmdIO{}

				Convey("When InitCMD is called", func() {
					cmd := InitCMD(cio)

					Convey("Then it should set opts.cmdIO and return the command", func() {
						So(cmd, ShouldNotBeNil)
						So(opts.cmdIO, ShouldEqual, cio)
						So(cmd.Use, ShouldEqual, "up") // 只验证基本属性
					})
				})
			})
		})

		// 测试 runUp
		Convey("runUp", func() {
			Convey("Given a valid config file path and Up succeeds", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				config := &utils.ClusterConfig{}
				patches := gomonkey.NewPatches()
				patches.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return config, nil
				})
				patches.ApplyFunc(clusterctrl.NewSSHRemoteExecutor, func(sshCmd string) clusterctrl.RemoteExecutor {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Up, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor) error {
					return nil
				})
				defer patches.Reset()

				Convey("When runUp is called", func() {
					err := runUp(cmd, args)

					Convey("Then it should return nil", func() {
						So(err, ShouldBeNil)
					})
				})
			})

			Convey("Given a config file path and LoadClusterConfig fails", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				patches := gomonkey.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return nil, errors.New("load config failed")
				})
				defer patches.Reset()

				Convey("When runUp is called", func() {
					err := runUp(cmd, args)

					Convey("Then it should return the load error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "load config failed")
					})
				})
			})

			Convey("Given a config file path and Up fails", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				config := &utils.ClusterConfig{}
				patches := gomonkey.NewPatches()
				patches.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return config, nil
				})
				patches.ApplyFunc(clusterctrl.NewSSHRemoteExecutor, func(sshCmd string) clusterctrl.RemoteExecutor {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Up, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor) error {
					return errors.New("up failed")
				})
				defer patches.Reset()

				Convey("When runUp is called", func() {
					err := runUp(cmd, args)

					Convey("Then it should return the up error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "up failed")
					})
				})
			})
		})
	})
}
