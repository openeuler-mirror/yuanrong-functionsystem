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

package stop

import (
	"cli/constant"
	"cli/pkg/cmdio"
	"cli/utils"
	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"
	"os"
	"path"
	"path/filepath"
	"testing"

	"github.com/spf13/cobra"
)

// Test function
func TestYrStopYuanRong(t *testing.T) {
	convey.Convey("Testing yrStopYuanRong", t, func() {
		cmd := &cobra.Command{}
		var args []string
		yrOpts.cmdIO = &cmdio.CmdIO{In: os.Stdin, Out: os.Stdout, ErrOut: os.Stderr}
		yuanRongDir := constant.YuanRongInstallationDir
		stopScriptPath := filepath.Join(yuanRongDir, "deploy", "process", "yr_stop.sh")

		convey.Convey("Given an unsupported OS", func() {
			patch := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return false
			})
			defer patch.Reset()

			convey.Convey("When calling yrStopYuanRong", func() {
				err := yrStopYuanRong(cmd, args)

				convey.Convey("Then it should return an error", func() {
					convey.So(err, convey.ShouldNotBeNil)
					convey.So(err.Error(), convey.ShouldContainSubstring, "unsupported os")
				})
			})
		})

		convey.Convey("Given a missing stop script", func() {
			patch := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patch.Reset()
			_ = os.RemoveAll(stopScriptPath) // Ensure file does not exist

			convey.Convey("When calling yrStopYuanRong", func() {
				err := yrStopYuanRong(cmd, args)

				convey.Convey("Then it should return an error", func() {
					convey.So(err, convey.ShouldNotBeNil)
					convey.So(err.Error(), convey.ShouldContainSubstring, "maybe check if you have installed yuanrong")
				})
			})
		})

		convey.Convey("Given a valid stop script", func() {
			_ = os.MkdirAll(path.Dir(stopScriptPath), os.ModeDir)
			_ = os.WriteFile(stopScriptPath, []byte("#!/bin/bash\necho stop"), 0755) // Create mock script
			patch := gomonkey.ApplyFunc(utils.GetGOOSType, func() (string, string) {
				return "bash", "-c"
			})
			defer patch.Reset()

			convey.Convey("When calling yrStopYuanRong", func() {
				err := yrStopYuanRong(cmd, args)

				convey.Convey("Then it should succeed without error", func() {
					convey.So(err, convey.ShouldBeNil)
				})
			})
		})
	})
}
