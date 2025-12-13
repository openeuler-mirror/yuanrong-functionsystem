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

// Package yrexec provides the exec command
package exec

import (
	"errors"
	"fmt"
	"os"
	"strings"

	"github.com/spf13/cobra"
	"golang.org/x/term"

	"cli/constant"
	"cli/pkg/clusterctrl"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

const (
	numExecPositionalArgs = 2
	defaultTerminalSize   = 80
)

type options struct {
	cmdIO         *cmdio.CmdIO
	clusterConfig *utils.ClusterConfig
	AlsoStart     bool
	AlsoStop      bool
	Env           []string
	Cmd           string
}

var opts options

var yrExecCmd = &cobra.Command{
	Use: "exec [flags] <cluster_config.yaml> -- <command>",
	Short: fmt.Sprintf("exec %s an command on the master node, and start/stop the yuanrong cluster (if need)",
		constant.PlatformName),
	Long: fmt.Sprintf(`exec %s an command on the master node, and start/stop the yuanrong cluster (if need)`,
		constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
$ %s exec cluster_config.yaml -- /path/to/program
`, constant.CliName)),
	Args: cobra.MinimumNArgs(numExecPositionalArgs),
	RunE: runExec,
}

// InitCMD init cmd
func InitCMD(cio *cmdio.CmdIO) *cobra.Command {
	opts.cmdIO = cio
	yrExecCmd.Flags().BoolVar(&opts.AlsoStart, "start", false, "")
	yrExecCmd.Flags().BoolVar(&opts.AlsoStop, "stop", false, "")
	yrExecCmd.Flags().StringSliceVarP(&opts.Env, "env", "e", []string{}, "specify environment")
	return yrExecCmd
}

func findOsArgsCommandAfterDoubleSlash() (string, error) {
	var cmd string
	for i, arg := range os.Args {
		if arg == "--" {
			cmd = strings.Join(os.Args[i+1:], " ")
			break
		}
	}
	if cmd == "" {
		return "", errors.New("failed to get command after `--`")
	}
	return cmd, nil
}

func getTerminalWidthSplitter(char string) string {
	width, _, err := term.GetSize(int(os.Stdout.Fd()))
	if err != nil {
		return strings.Repeat(char, defaultTerminalSize)
	}
	return strings.Repeat(char, width)
}

func runExec(_ *cobra.Command, args []string) error {
	clusterConfigFilePath := args[0]
	clusterConfigCmd, err := findOsArgsCommandAfterDoubleSlash()
	if err != nil {
		return err
	}

	clusterConfig, err := utils.LoadClusterConfig(clusterConfigFilePath)
	if err != nil {
		return err
	}
	clusterConfig.Env = append(clusterConfig.Env, opts.Env...)

	executor := clusterctrl.NewSSHRemoteExecutor(clusterConfig.SSHCmd)

	defer func() {
		if opts.AlsoStop {
			if err := clusterctrl.Down(clusterConfig, executor); err != nil {
				colorprint.PrintInteractive(os.Stdout, fmt.Sprintf("failed to stop, err: %s", err.Error()))
			}
		}
	}()

	if opts.AlsoStart {
		if err := clusterctrl.Up(clusterConfig, executor); err != nil {
			return err
		}
	}
	if wd, err := os.Getwd(); err == nil {
		clusterConfigCmd = fmt.Sprintf("cd %s ;", wd) + clusterConfigCmd
	}

	colorprint.PrintSuccess(os.Stdout, getTerminalWidthSplitter("=")+"\n", "")
	execErr := clusterctrl.Exec(clusterConfig, executor, clusterConfigCmd)
	colorprint.PrintSuccess(os.Stdout, getTerminalWidthSplitter("=")+"\n", "")

	return execErr
}
