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

// Package stop is start cmd
package stop

import (
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/spf13/cobra"

	"cli/constant"
	"cli/pkg/cmdio"
	"cli/pkg/progress"
	"cli/utils"
	"cli/utils/colorprint"
)

type yrStopOptions struct {
	cmdIO            *cmdio.CmdIO
	graceExitTimeout int
}

var yrOpts yrStopOptions

var yrStopCmd = &cobra.Command{
	Use:   "stop",
	Short: fmt.Sprintf("stop %s Platform", constant.PlatformName),
	Long:  fmt.Sprintf(`stop %s Platform`, constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
    %s stop in non-interactive mode:
    $ %s stop
	`, constant.CliName, constant.CliName)),
	Args: utils.NoArgs,
	RunE: yrStopYuanRong,
}

const (
	defaultGraceExitTimeout = 5
)

var (
	errorGetZeroYuanRongProcesses = errors.New("didn't find any yuanrong processes")
)

// InitYrCMD init login cmd
func InitYrCMD(cio *cmdio.CmdIO) *cobra.Command {
	yrOpts.cmdIO = cio

	yrStopCmd.Flags().IntVarP(&yrOpts.graceExitTimeout, "grace_exit_timeout", "g", defaultGraceExitTimeout,
		fmt.Sprintf("grace exit timeout given yuanrong processes, "+
			"after this timeout the processes will be killed by force. "+
			"(in seconds, default to %d)", defaultGraceExitTimeout))

	return yrStopCmd
}

func yrStopYuanRong(cmd *cobra.Command, args []string) error {
	if !utils.SupportSystem() {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "Failed to stop, ",
			fmt.Sprintf("unsupported os: %s", runtime.GOOS), ".\n")
		return errors.New(fmt.Sprintf("unsupported os: %s", runtime.GOOS))
	}
	yuanRongDir, err := filepath.Abs(constant.YuanRongInstallationDir)
	if err != nil {
		return err
	}
	var keywords = []string{
		filepath.Join(yuanRongDir, "deploy", "process", "deploy.sh"),
		filepath.Join(yuanRongDir, "function_system"),
		filepath.Join(yuanRongDir, "data_system"),
		filepath.Join(yuanRongDir, "third_party"),
		filepath.Join(yuanRongDir, "runtime"),
	}

	clearCurrentMasterInfoSymbolLink()
	err = stopYuanRongProcesses(keywords, time.Duration(yrOpts.graceExitTimeout)*time.Second)
	if errors.Is(err, errorGetZeroYuanRongProcesses) {
		colorprint.PrintSuccess(yrOpts.cmdIO.Out, "Did not find any active Yuanrong processes.", "\n")
	} else {
		colorprint.PrintSuccess(yrOpts.cmdIO.Out, "Yuanrong stop succeed.", "\n")
	}
	return nil
}

func clearCurrentMasterInfoSymbolLink() {
	if err := os.RemoveAll(constant.DefaultYuanRongCurrentMasterInfoPath); err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, fmt.Sprintf("Failed to remove dumped master info at %s, ",
			constant.DefaultYuanRongCurrentMasterInfoPath),
			err.Error(), ".\n")
	}
}

type process struct {
	PID     int
	Cmdline string
	Comm    string
	Args    []string
	Keyword string
}

// 获取所有进程列表
func getProcesses() ([]process, error) {
	var processes []process
	procDir, err := os.ReadDir("/proc")
	if err != nil {
		return nil, err
	}
	for _, entry := range procDir {
		if !entry.IsDir() {
			continue
		}
		pidStr := entry.Name()
		pid, err := strconv.Atoi(pidStr)
		if err != nil {
			continue
		}
		// 读取 /proc/<pid>/cmdline
		cmdlinePath := path.Join("/proc", pidStr, "cmdline")
		cmdlineBytes, err := os.ReadFile(cmdlinePath)
		if err != nil {
			continue // 进程可能已退出，跳过
		}

		cmdline := strings.Split(string(cmdlineBytes), "\x00")
		if len(cmdline) == 0 {
			continue
		}
		exePath := cmdline[0]
		args := cmdline[1:]
		processes = append(processes, process{PID: pid, Cmdline: exePath, Comm: path.Base(exePath), Args: args})
	}
	return processes, nil
}

func filterProcesses(processes []process, keywords []string) []process {
	var filtered []process
	keywordSet := make(map[string]bool)
	for _, keyword := range keywords {
		keywordSet[keyword] = true
	}
	for _, p := range processes {
		for keyword := range keywordSet {
			if strings.Contains(p.Cmdline, keyword) {
				p.Keyword = keyword
				filtered = append(filtered, p)
				break
			}
			filtered = filterProcessArgs(p, keyword, filtered)
		}
	}
	return filtered
}

func filterProcessArgs(p process, keyword string, filtered []process) []process {
	for _, arg := range p.Args {
		if strings.Contains(arg, keyword) {
			p.Keyword = keyword
			return append(filtered, p)
		}
	}
	return filtered
}

func groupProcessesByKeywords(processes []process, keywords []string) []process {
	var groups [][]process
	for _, keyword := range keywords {
		var group []process
		for _, p := range processes {
			if p.Keyword == keyword {
				group = append(group, p)
			}
		}
		groups = append(groups, group)
	}

	processed := make(map[int]bool)
	var orderedProcesses []process
	for _, group := range groups {
		for _, p := range group {
			if !processed[p.PID] {
				orderedProcesses = append(orderedProcesses, p)
				processed[p.PID] = true
			}
		}
	}
	return orderedProcesses
}

func getProcessName(p process) string {
	// bash need more process
	if p.Comm == "bash" {
		for _, arg := range p.Args {
			if strings.Contains(arg, "deploy.sh") {
				return "deploy.sh"
			}
		}
	}
	return p.Comm
}

func handleProcess(p process, graceExitTimeout time.Duration) {
	proc, err := os.FindProcess(p.PID)
	if err != nil {
		return
	}

	err = proc.Signal(syscall.SIGTERM)
	if err != nil {
		return
	}

	select {
	case <-time.After(graceExitTimeout):
		colorMap := []colorprint.StringColorInfo{
			{Str: fmt.Sprintf("process %s(%d) is force killed after %ds\n",
				getProcessName(p), p.PID, int(graceExitTimeout.Seconds())), Color: colorprint.InteractiveColor},
		}
		colorprint.PrintKeywords(yrOpts.cmdIO.Out, colorMap)
		err = proc.Signal(syscall.SIGKILL)
		if err != nil {
			return
		}
	case <-checkProcessExit(p.PID):
		colorprint.PrintSuccess(yrOpts.cmdIO.Out, fmt.Sprintf("process %s(%d) exited\n",
			getProcessName(p), p.PID), "")
	}
}

func checkProcessExit(pid int) <-chan bool {
	ch := make(chan bool)
	go func() {
		for {
			proc, err := os.FindProcess(pid)
			if err != nil {
				ch <- true
				return
			}
			err = proc.Signal(syscall.Signal(0))
			if err != nil {
				ch <- true
				return
			}
			time.Sleep(time.Millisecond * 100)
		}
	}()
	return ch
}

func stopYuanRongProcesses(keywords []string, graceExitTimeout time.Duration) error {
	processes, err := getProcesses()
	if err != nil {
		return err
	}

	filteredProcesses := filterProcesses(processes, keywords)
	orderedProcesses := groupProcessesByKeywords(filteredProcesses, keywords)
	total := len(orderedProcesses)
	if total == 0 {
		return errorGetZeroYuanRongProcesses
	}
	fmt.Printf("find %d Yuanrong processes\n", total)

	stopProgress := progress.CreateAndDoing("Stopping Yuanrong processes")
	var wg sync.WaitGroup
	for _, p := range orderedProcesses {
		wg.Add(1)
		go func(p process) {
			defer wg.Done()
			handleProcess(p, graceExitTimeout)
		}(p)
	}

	wg.Wait()
	stopProgress.Done("Stop Yuanrong processes")
	return nil
}
