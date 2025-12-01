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

// Package version is version cmd of CLI
package version

import (
	"github.com/spf13/cobra"

	"cli/constant"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

type options struct {
	cmdIO *cmdio.CmdIO
}

var opts = &options{}

var cmd = &cobra.Command{
	Use:   "version",
	Short: "print the cli's version information",
	Long:  `print the cli's version information`,
	Args:  utils.NoArgs,
	Run: func(cmd *cobra.Command, args []string) {
		colorMap := []colorprint.StringColorInfo{
			{Str: "CLI version: ", Color: colorprint.Colorless},
			{Str: constant.Version, Color: colorprint.KeywordColor},
			{Str: ".\n", Color: colorprint.Colorless},
			{Str: "Using yuanrong at: ", Color: colorprint.Colorless},
			{Str: constant.YuanRongInstallationDir, Color: colorprint.Colorless},
			{Str: "\n", Color: colorprint.Colorless},
		}
		colorprint.PrintKeywords(opts.cmdIO.Out, colorMap)
	},
}

// InitVersionCMD init cmd for version
func InitVersionCMD(cio *cmdio.CmdIO) *cobra.Command {
	opts.cmdIO = cio
	return cmd
}
