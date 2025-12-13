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

package exec

import (
	"errors"
	"os"
	"testing"

	"github.com/agiledragon/gomonkey/v2"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/spf13/cobra"

	"cli/pkg/clusterctrl"
	"cli/pkg/cmdio"
	"cli/utils"
)

func TestExecCmd(t *testing.T) {
	Convey("Test Exec Command", t, func() {
		// 测试 InitCMD
		Convey("InitCMD", func() {
			Convey("Given a CmdIO instance", func() {
				cio := &cmdio.CmdIO{}

				Convey("When InitCMD is called", func() {
					cmd := InitCMD(cio)

					Convey("Then it should set opts.cmdIO and configure flags", func() {
						So(cmd, ShouldNotBeNil)
						So(opts.cmdIO, ShouldEqual, cio)
						So(cmd.Use, ShouldEqual, "exec [flags] <cluster_config.yaml> -- <command>")
						So(cmd.Flags().Lookup("start"), ShouldNotBeNil)
						So(cmd.Flags().Lookup("stop"), ShouldNotBeNil)
					})
				})
			})
		})

		// 测试 runExec
		Convey("runExec", func() {
			// 重置 os.Args 以便测试
			origArgs := os.Args
			defer func() { os.Args = origArgs }()

			Convey("Given valid args and successful execution", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml", "--", "echo hello"}
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
				patches.ApplyFunc(clusterctrl.Exec, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor, cmd string) error {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Down, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor) error {
					return nil
				})
				defer patches.Reset()

				Convey("When runExec is called with default flags", func() {
					err := runExec(cmd, args)

					Convey("Then it should return nil", func() {
						So(err, ShouldBeNil)
					})
				})

				Convey("When runExec is called with --start and --stop", func() {
					opts.AlsoStart = true
					opts.AlsoStop = true
					defer func() { opts.AlsoStart = false; opts.AlsoStop = false }()
					err := runExec(cmd, args)

					Convey("Then it should return nil", func() {
						So(err, ShouldBeNil)
					})
				})
			})

			Convey("Given args without `--` command", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml"} // 缺少 --

				Convey("When runExec is called", func() {
					err := runExec(cmd, args)

					Convey("Then it should return an error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "failed to get command after `--`")
					})
				})
			})

			Convey("Given a config file path and LoadClusterConfig fails", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml", "--", "echo hello"}
				patches := gomonkey.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return nil, errors.New("load config failed")
				})
				defer patches.Reset()

				Convey("When runExec is called", func() {
					err := runExec(cmd, args)

					Convey("Then it should return the load error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "load config failed")
					})
				})
			})

			Convey("Given a failure in Up with --start", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml", "--", "echo hello"}
				config := &utils.ClusterConfig{}
				opts.AlsoStart = true
				defer func() { opts.AlsoStart = false }()
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

				Convey("When runExec is called", func() {
					err := runExec(cmd, args)

					Convey("Then it should return the up error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "up failed")
					})
				})
			})

			Convey("Given a failure in Exec", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml", "--", "echo hello"}
				config := &utils.ClusterConfig{}
				patches := gomonkey.NewPatches()
				patches.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return config, nil
				})
				patches.ApplyFunc(clusterctrl.NewSSHRemoteExecutor, func(sshCmd string) clusterctrl.RemoteExecutor {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Exec, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor, cmd string) error {
					return errors.New("exec failed")
				})
				defer patches.Reset()

				Convey("When runExec is called", func() {
					err := runExec(cmd, args)

					Convey("Then it should return the exec error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "exec failed")
					})
				})
			})

			Convey("Given a failure in Down with --stop", func() {
				cmd := &cobra.Command{}
				args := []string{"config.yaml"}
				os.Args = []string{"test", "exec", "config.yaml", "--", "echo hello"}
				config := &utils.ClusterConfig{}
				opts.AlsoStop = true
				defer func() { opts.AlsoStop = false }()
				patches := gomonkey.NewPatches()
				patches.ApplyFunc(utils.LoadClusterConfig, func(filePath string) (*utils.ClusterConfig, error) {
					return config, nil
				})
				patches.ApplyFunc(clusterctrl.NewSSHRemoteExecutor, func(sshCmd string) clusterctrl.RemoteExecutor {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Exec, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor, cmd string) error {
					return nil
				})
				patches.ApplyFunc(clusterctrl.Down, func(cfg *utils.ClusterConfig, executor clusterctrl.RemoteExecutor) error {
					return errors.New("down failed")
				})
				defer patches.Reset()

				Convey("When runExec is called", func() {
					err := runExec(cmd, args)

					Convey("Then it should return the down error", func() {
						So(err, ShouldNotBeNil)
						So(err.Error(), ShouldEqual, "down failed")
					})
				})
			})
		})
	})
}
