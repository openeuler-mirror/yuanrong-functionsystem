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

// Package down provides the down command
package down

import (
	"fmt"

	"github.com/spf13/cobra"

	"cli/constant"
	"cli/pkg/clusterctrl"
	"cli/pkg/cmdio"
	"cli/utils"
)

const (
	numDownPositionalArgs = 1
)

type options struct {
	cmdIO         *cmdio.CmdIO
	clusterConfig *utils.ClusterConfig
}

var opts options

var yrDownCmd = &cobra.Command{
	Use:   "down",
	Short: fmt.Sprintf("start %s Platform", constant.PlatformName),
	Long:  fmt.Sprintf(`start %s Platform`, constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
$ %s up cluster_config.yaml
`, constant.CliName)),
	Args: cobra.ExactArgs(numDownPositionalArgs),
	RunE: runDown,
}

// InitCMD init cmd
func InitCMD(cio *cmdio.CmdIO) *cobra.Command {
	opts.cmdIO = cio
	return yrDownCmd
}

func runDown(_ *cobra.Command, args []string) error {
	clusterConfigFilePath := args[0]
	config, err := utils.LoadClusterConfig(clusterConfigFilePath)
	if err != nil {
		return err
	}
	return clusterctrl.Down(config, clusterctrl.NewSSHRemoteExecutor(config.SSHCmd))
}
