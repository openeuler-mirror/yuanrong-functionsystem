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

// Package debug is debug cmd of debug
package debug

import (
	"bufio"
	"fmt"
	"os/exec"
	"strconv"
	"strings"

	"github.com/spf13/cobra"

	"cli/constant"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

// debugOptions 结构体用于存储命令行参数和相关配置
type debugOptions struct {
	cmdIO  *cmdio.CmdIO
	master string
}

const (
	// 调试 Shell 的提示符
	promptString = "(yrdb) "
	// 拆分命令和参数
	splitpart = 2
)

var (
	opts debugOptions
	// MasterInfo 存储 master.info 文件的内容
	MasterInfo *utils.MasterInfo
)

var yrDebugCmd = &cobra.Command{
	Use:   "debug",
	Short: fmt.Sprintf("Enter the %s debug shell", constant.PlatformName),
	Long:  fmt.Sprintf("Enter the %s debug shell", constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
    %s debug in interactive mode:
    $ %s debug

    %s debug with specified file path: 'master.info':
    $ %s debug --master master.info
	`, constant.CliName, constant.CliName, constant.CliName, constant.CliName)),
	Args: utils.NoArgs,
	RunE: yrDebug,
}

// CommandHandler 定义命令处理函数的类型
type CommandHandler func(*cmdio.CmdIO, []string) error

// commandTable 定义命令表
var commandTable = map[string]CommandHandler{
	"help":     handleHelp,
	"h":        handleHelp,
	"quit":     handleQuit,
	"q":        handleQuit,
	"instance": handleInstance,
	"i":        handleInstance,
	"info":     handleInfo,
}

// InitCMD 初始化 debug 命令
func InitCMD(cio *cmdio.CmdIO) *cobra.Command {
	opts.cmdIO = cio
	MasterInfo = &utils.MasterInfo{}                      // 初始化 MasterInfo
	debugInstanceInfosMap = make(map[string]InstanceInfo) // 初始化 debugInstanceInfosMap
	yrDebugCmd.Flags().StringVarP(&opts.master, "master", "m", "", "Specify the 'master.info' file path")
	return yrDebugCmd
}

// yrDebug 是 debug 命令的执行函数
func yrDebug(cmd *cobra.Command, args []string) error {
	masterPath := opts.master
	if masterPath == "" {
		masterPath = constant.DefaultYuanRongCurrentMasterInfoPath
	}
	var err error
	MasterInfo, err = utils.GetMasterInfoFromFile(masterPath)
	if err != nil {
		colorprint.PrintFail(opts.cmdIO.Out, "failed to load master.info: ", err.Error(), "\n")
		return err
	}
	startDebugShell()
	return nil
}

// startDebugShell 启动调试 Shell
func startDebugShell() {
	colorprint.PrintInteractive(opts.cmdIO.Out, "Type 'help' or 'h' for help, 'quit' or 'q' to exit.\n")
	reader := bufio.NewReader(opts.cmdIO.In) // 使用 bufio.NewReader 读取输入
	for {
		colorprint.PrintInteractive(opts.cmdIO.Out, promptString) // 提示符

		// 使用 bufio.NewReader 读取用户输入
		input, err := reader.ReadString('\n')
		if err != nil {
			colorprint.PrintFail(opts.cmdIO.Out, "failed to read input: ", err.Error(), "\n")
			continue
		}
		input = strings.TrimSpace(input) // 去除多余的空白字符

		// 将输入拆分为命令和参数
		parts := strings.Fields(input)
		if len(parts) == 0 {
			continue // 如果没有输入任何内容，跳过
		}

		// 获取命令处理函数
		handler, ok := commandTable[strings.ToLower(parts[0])]
		if !ok {
			colorprint.PrintFail(opts.cmdIO.Out, "unknown command: ", input, "\n")
			colorprint.PrintInteractive(opts.cmdIO.Out, "Type 'help' or 'h' for a list of available commands.\n")
			continue
		}

		// 调用命令处理函数
		if err := handler(opts.cmdIO, parts); err != nil {
			if err.Error() == "exit" {
				// 如果返回的错误是 "exit"，则退出循环
				return
			}
			colorprint.PrintFail(opts.cmdIO.Out, err.Error(), "\n", "")
		}
	}
}

// handleHelp 处理 help 命令
func handleHelp(cio *cmdio.CmdIO, parts []string) error {
	colorprint.PrintSuccess(opts.cmdIO.Out, "Available commands:\n", "")
	colorprint.PrintSuccess(opts.cmdIO.Out, "  help/h: Show this help message\n", "")
	colorprint.PrintSuccess(opts.cmdIO.Out, "  instance/i <id>: Attach to the specified remote instance\n", "")
	colorprint.PrintSuccess(opts.cmdIO.Out, "  info instance/i: Show all instance ids and process status\n", "")
	colorprint.PrintSuccess(opts.cmdIO.Out, "  quit/q: Exit the debug shell\n", "")
	return nil
}

// handleQuit 处理 quit 命令
func handleQuit(cio *cmdio.CmdIO, parts []string) error {
	colorprint.PrintSuccess(opts.cmdIO.Out, "Exiting debug shell.\n", "")
	return fmt.Errorf("exit") // 返回错误以退出循环
}

// handleInstance 处理 instance <id> 命令
func handleInstance(cio *cmdio.CmdIO, parts []string) error {
	if len(parts) != splitpart {
		return fmt.Errorf("invalid command. Usage: instance <id>")
	}

	instanceID := parts[1]
	colorprint.PrintInteractive(opts.cmdIO.Out, fmt.Sprintf("Attaching to instance ID: %s\n", instanceID))

	// 从 debugInstanceInfosMap 中获取实例信息
	instanceInfo, ok := debugInstanceInfosMap[instanceID]
	if !ok {
		// 发起一次查询，刷新 debugInstanceInfosMap
		if err := queryDebugInstanceInfo(cio); err != nil {
			return fmt.Errorf("failed to query debug instance info, err: %v", err)
		}
		if instanceInfo, ok = debugInstanceInfosMap[instanceID]; !ok {
			return fmt.Errorf("failed to find instance ID %s", instanceID)
		}
	}

	// 启动 GDB 客户端并连接到 gdbserver
	if err := startGDBClient(instanceInfo.DebugServer, strconv.Itoa(int(instanceInfo.PID))); err != nil {
		return err
	}

	return nil
}

// startGDBClient 启动 GDB 客户端并连接到 gdbserver
func startGDBClient(gdbserverAddress string, pid string) error {
	// 构造 GDB 客户端命令
	cmd := exec.Command(
		"gdb",
		"-ex", "set pagination off",
		"-ex", fmt.Sprintf("target extended-remote %s", gdbserverAddress),
		"-ex", fmt.Sprintf("attach %s", pid),
		"-ex", "handle SIGSTOP nostop",
	)

	// 将 GDB 的标准输入、输出和错误重定向到 opts.cmdIO
	cmd.Stdin = opts.cmdIO.In
	cmd.Stdout = opts.cmdIO.Out
	cmd.Stderr = opts.cmdIO.ErrOut

	// 启动 GDB 客户端
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start GDB client: %v", err)
	}

	// 等待 GDB 客户端退出
	if err := cmd.Wait(); err != nil {
		return fmt.Errorf("GDB client exited with error: %v", err)
	}

	return nil
}

// handleInfo 处理 info 命令
func handleInfo(cio *cmdio.CmdIO, parts []string) error {
	if len(parts) < splitpart {
		return fmt.Errorf("invalid command. Usage: info <subcommand>")
	}
	subcommand := strings.ToLower(parts[1])
	switch subcommand {
	case "instance", "i":
		pageOutDebugInstanceInfos(cio)
		return nil
	default:
		return fmt.Errorf("unknown subcommand: %s", subcommand)
	}
	return nil
}
