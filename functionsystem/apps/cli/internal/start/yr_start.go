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

// Package start is start cmd
package start

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/spf13/cobra"

	"cli/constant"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

type yrStartOptions struct {
	cmdIO *cmdio.CmdIO

	// General Options:
	ipAddress             string
	cpuNum                string
	deployPath            string
	memoryNum             string
	sharedMemoryNum       string
	masterInfoOutput      string
	master                bool
	masterInfo            string
	servicesPath          string
	masterIp              string
	enableInheritEnv      string
	etcdAddrList          string
	dsNodeDeadTimeoutS    string
	dsNodeTimeoutS        string
	dsHeartbeatIntervalMs string

	// startup options
	disableNcCheck bool

	// Others
	verbose int
	block   bool
}

var yrOpts yrStartOptions

const fileMode = 0600
const dirMode = os.ModeDir | 0750
const deployFailureWaitTime = 2 * constant.DeployStdLogRedirectIntervalMs * time.Millisecond

var ipRegex *regexp.Regexp = regexp.MustCompile(`(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})`)

type currentMasterInfoDumpPolicy int

const (
	dumpPolicyNothing currentMasterInfoDumpPolicy = iota + 1
	dumpPolicyNew
	dumpPolicyReplace
	dumpPolicyInvalid
)

var yrStartCmd = &cobra.Command{
	Use:   "start",
	Short: fmt.Sprintf("start %s Platform", constant.PlatformName),
	Long:  fmt.Sprintf(`start %s Platform`, constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
    %s start in non-interactive mode:
    $ %s start --master
	`, constant.CliName, constant.CliName)),
	Args: cobra.ArbitraryArgs,
	RunE: yrStartDeploy,
}

// InitYrCMD init cmd
func InitYrCMD(cio *cmdio.CmdIO) *cobra.Command {
	yrOpts.cmdIO = cio
	mem := &runtime.MemStats{}
	runtime.ReadMemStats(mem)

	totalMem := mem.Sys
	if physicalMem, err := utils.ReadMemoryStats(); err == nil {
		totalMem = physicalMem.MemTotal
	}

	yuanRongCpu := runtime.NumCPU() * 1000 // 这里cpu的单位是千分之一核，默认使用所有系统资源
	yuanRongMem := totalMem / 1024         // openYuanRong 默认纳管集群所有内存
	dataSystemMem := yuanRongMem / 3       // 共享内存默认使用1/3的内存

	yrStartCmd.SilenceUsage = true
	yrStartCmd.FParseErrWhitelist.UnknownFlags = true

	defineParams(yuanRongCpu, yuanRongMem, dataSystemMem)

	// Logging Options: None
	// Function System Options: None
	// Data System Options: None
	yrStartCmd.SetHelpFunc(func(command *cobra.Command, strings []string) {
		helpInfo, err := yrDeployShowHelp(constant.YuanRongInstallationDir)
		if err != nil {
			colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "failed to get script help info", err.Error(), "\n")
		}
		colorprint.PrintSuccess(yrOpts.cmdIO.Out, "", helpInfo)
	})
	return yrStartCmd
}

func defineParams(yuanRongCpu int, yuanRongMem uint64, dataSystemMem uint64) {
	// In the tools, we override some options to provide better usability
	// General Options:
	yrStartCmd.Flags().BoolVarP(&yrOpts.master, "master", "", false,
		"deploy control plane, otherwise only deploy data plane")
	yrStartCmd.Flags().IntVarP(&yrOpts.verbose, "verbose", "", 2, "no output")
	yrStartCmd.Flags().BoolVarP(&yrOpts.block, "block", "", false,
		"block, not running background")
	yrStartCmd.Flags().StringVarP(&yrOpts.ipAddress, "ip_address", "a", "",
		"node ip address")
	yrStartCmd.Flags().StringVarP(&yrOpts.cpuNum, "cpu_num", "c", strconv.Itoa(yuanRongCpu),
		"overall cpu cores (1/1000 core) in current script context")
	yrStartCmd.Flags().StringVarP(&yrOpts.deployPath, "deploy_path", "d", "",
		"it is recommended to use an empty directory as deploy_path")
	yrStartCmd.Flags().StringVarP(&yrOpts.memoryNum, "memory_num", "m",
		strconv.FormatUint(yuanRongMem, 10),
		"overall memory (MB) in current script context(should be larger than shared_memory_num)")
	yrStartCmd.Flags().StringVarP(&yrOpts.sharedMemoryNum, "shared_memory_num", "s",
		strconv.FormatUint(dataSystemMem, 10),
		"data system shared memory (MB) should be reserved in current script context")
	yrStartCmd.Flags().StringVarP(&yrOpts.masterInfoOutput, "master_info_output", "o", "",
		"master info output file, absolute path")
	yrStartCmd.Flags().StringVarP(&yrOpts.masterInfo, "master_info", "", "", "master info")
	yrStartCmd.Flags().StringVarP(&yrOpts.servicesPath, "services_path", "p", "",
		"auto load function path")
	yrStartCmd.Flags().StringVarP(&yrOpts.masterIp, "master_ip", "", "",
		"master ip for data plane component")
	yrStartCmd.Flags().StringVarP(&yrOpts.etcdAddrList, "etcd_addr_list", "", "",
		"etcd cluster address list, such as ip1,ip2 or ip1:port1,ip2:port2 or "+
			"ip1:port1:peer_port1,ip2:port2:peer_port2. if use this param, master_info is not required.")
	yrStartCmd.Flags().StringVarP(&yrOpts.enableInheritEnv, "enable_inherit_env", "", "true",
		"enable runtime to inherit env from runtime-manager, default is true")
	yrStartCmd.Flags().StringVarP(&yrOpts.dsNodeDeadTimeoutS, "ds_node_dead_timeout_s", "", "30",
		"datasystem node dead timeout (in second, default is 30)")
	yrStartCmd.Flags().StringVarP(&yrOpts.dsNodeTimeoutS, "ds_node_timeout_s", "", "10",
		"datasystem node timeout (in second, default is 10)")
	yrStartCmd.Flags().StringVarP(&yrOpts.dsHeartbeatIntervalMs, "ds_heartbeat_interval_ms", "", "3000",
		"datasystem heartbeat interval (in ms, default is 3000)")

	yrStartCmd.Flags().BoolVarP(&yrOpts.disableNcCheck, "disable_nc_check", "", true,
		"disable nc check port (default true)")
}

func yrDeployShowHelp(path string) (string, error) {
	// make sure fresh deploy.sh script exists and valid
	deployScriptPath := filepath.Join(path, "deploy", "process", "deploy.sh")
	if _, err := os.Stat(deployScriptPath); err != nil {
		return "", errors.New(
			fmt.Sprintf("maybe check if you have installed yuanrong (%s)", err.Error()))
	}

	execName, _ := utils.GetGOOSType()
	args := make([]string, 0)
	args = append(args, deployScriptPath)
	args = append(args, "--help")
	masterShowHelpCommand := exec.Command(execName, args...)
	output, err := masterShowHelpCommand.Output()
	return string(output), err
}

func yrStartDeploy(cmd *cobra.Command, args []string) error {
	if err := yrStartYuanRong(cmd, args); err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "", "Deploy OpenYuanrong failed.", "\n")
		return err
	}
	colorprint.PrintSuccess(yrOpts.cmdIO.Out, "Succeeded", " to start!")
	return nil
}

func validateStartParams(cmd *cobra.Command) error {
	if yrOpts.master && cmd.Flags().Changed("master_info") {
		return errors.New("--master, --master_info cannot be specified together")
	}
	if cmd.Flags().Changed("etcd_addr_list") && cmd.Flags().Changed("master_info") {
		return errors.New("--etcd_addr_list, --master_info cannot be specified together")
	}
	if !yrOpts.master && yrOpts.etcdAddrList == "" && yrOpts.masterInfo == "" {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "",
			"none of --master, --etcd_addr_list or --master_info is specified", `
* trying to start your first yuanrong node, please use --master
  yr start --master

* trying to start your second yuanrong node, please use --master_info as the instruction shown by the above command
`)
		return errors.New("")
	}
	return nil
}

func yrStartYuanRong(cmd *cobra.Command, args []string) error {
	if !utils.SupportSystem() {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "Failed to start, ",
			fmt.Sprintf("unsupported os: %s", runtime.GOOS), ".\n")
		return errors.New(fmt.Sprintf("unsupported os: %s", runtime.GOOS))
	}

	policy, err := checkIfNodeAlreadyStartedYuanRong()
	if err != nil {
		return err
	}

	yrProcessDeployArgs()
	yuanRongDir := constant.YuanRongInstallationDir

	if err := validateStartParams(cmd); err != nil {
		return err
	}

	if err := yrMakeDeployPathSymbolLink(); err != nil {
		return err
	}
	if yrOpts.master { // start master
		return yrDeployMaster(yuanRongDir, policy)
	}
	if err := yrDeployAgent(yuanRongDir); err != nil {
		return err
	}
	if err := yrMakeSymbolLink(policy); err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "warn: make symbol link may failed. ", err.Error(), "\n")
	}
	return nil
}

func checkIfNodeAlreadyStartedYuanRong() (currentMasterInfoDumpPolicy, error) {
	// Overall: always allow user start yuanrong over an existing cluster,
	//          if start another agent in already belonged cluster's master node, do nothing
	//          if start anything else, log the replace, point to the newer yuanrong processes
	//
	// if not exist, ok, just return
	// if exist, then
	//  * if existing info is master, and incoming is also in the same yuanrong cluster
	//      then do nothing
	//  * else, make a replacement, log a warning about the replacement, and then point to the new processes
	//
	// to check if belongs to same master, we use etcd_addr_list(if exists) or master_ip(if exists)
	var err error
	info, err := utils.GetMasterInfoFromFile(constant.DefaultYuanRongCurrentMasterInfoPath)
	if err != nil {
		if os.IsNotExist(err) {
			return dumpPolicyNew, nil
		}
		return dumpPolicyInvalid, errors.New("maybe you have started a yuanrong cluster in your environment, and we " +
			"failed to read its info, run `yr stop` to make sure it is fully cleared, and then you can start again")
	}

	if !info.IsMasterNode() {
		// if current is agent, just replace it
		return dumpPolicyReplace, nil
	}

	var incomingMi *utils.MasterInfo
	if yrOpts.masterInfo != "" {
		// start a single-master cluster's agent
		incomingMi = utils.ExtractMasterInfo(yrOpts.masterInfo)
	} else {
		// maybe, starting another single-master cluster's master -- no etcdAddrList, always should return REPLACE
		// maybe, starting a multi-master cluster's master        -- has etcdAddrList, check if the same
		// maybe, starting a multi-master cluster's agent         -- has etcdAddrList, check if the same
		incomingMi = &utils.MasterInfo{
			EtcdAddrList: splitEtcdAddrListString(yrOpts.etcdAddrList),
		}
	}

	if info.IsSameCluster(incomingMi) {
		return dumpPolicyNothing, nil
	}
	return dumpPolicyReplace, nil
}

func splitEtcdAddrListString(etcdAddrList string) []string {
	var list []string
	if ipRegex == nil {
		return list
	}
	for _, ip := range ipRegex.FindAllString(etcdAddrList, -1) {
		list = append(list, ip)
	}
	return list
}

func printMasterStartedHelpMsg() error {

	if masterInfo, err := utils.GetMasterInfoString(constant.DefaultYuanRongCurrentMasterInfoPath); err == nil &&
		yrOpts.verbose > 0 {
		var startNextHelp string
		if len(yrOpts.etcdAddrList) > 0 {
			startNextHelp = ""
		} else {
			startNextHelp = fmt.Sprintf(`  * to add another node into yuanrong cluster
    yr start --master_info "%s"
`, masterInfo)
		}

		colorprint.PrintSuccess(yrOpts.cmdIO.Out, "Yuanrong deployed succeed",
			fmt.Sprintf(`
Cluster master info:
    %s

Next Steps:
%s
  * to check if yuanrong cluster is running normally
    yr status

  * to stop running yuanrong processes
    yr stop
`, masterInfo, startNextHelp))
		return nil
	}
	if yrOpts.verbose > 0 {
		colorprint.PrintFail(yrOpts.cmdIO.Out, "Yuanrong deployed may failed",
			fmt.Sprintf(`
failed to find master info file at %s, the deployment may failed, you can also run
  yr stop
to make sure no yuanrong processes left on you machine
`, constant.DefaultYuanRongCurrentMasterInfoPath), ".\n")
	}
	return errors.New("failed to deploy yuanrong master")
}

func redirectFileContentToStd(ctx context.Context, deployStdFile *os.File) {
	var lastSize int64 = 0
	for {
		select {
		case <-ctx.Done():
			return
		default:
			time.Sleep(constant.DeployStdLogRedirectIntervalMs * time.Millisecond)
			fi, err := deployStdFile.Stat()
			if err != nil {
				colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "failed to stat log file: ", err.Error(), ".\n")
				return
			}
			if fi.Size() <= lastSize {
				time.Sleep(constant.DeployStdLogRedirectIntervalMs * time.Millisecond)
				continue
			}
			if _, err := deployStdFile.Seek(lastSize, io.SeekStart); err != nil {
				colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "failed to seek log file: ", err.Error(), ".\n")
				return
			}
			buffer := make([]byte, fi.Size()-lastSize)
			n, err := deployStdFile.Read(buffer)
			if err != nil && err != io.EOF {
				colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "failed to read log file: ", err.Error(), ".\n")
				return
			}
			if n > 0 {
				fmt.Print(string(buffer[:n]))
			}
			lastSize = fi.Size()
		}
	}
}

func prepareDeployWorkingDir() (*os.File, error) {
	// make sure fresh new `master_pid_port.txt`
	if err := os.Remove(filepath.Join(yrOpts.deployPath, "master_pid_port.txt")); err != nil {
		// do nothing
	}

	// make sure master info output file parent dir exists
	if err := os.MkdirAll(filepath.Dir(yrOpts.masterInfoOutput), dirMode); err != nil {
		return nil, err
	}
	// make sure the master info output file fresh new
	if err := os.RemoveAll(yrOpts.masterInfoOutput); err != nil {
		return nil, err
	}
	// make sure deploy path exists
	if err := os.MkdirAll(yrOpts.deployPath, dirMode); err != nil {
		return nil, err
	}
	// make sure /tmp/yr_sessions dir exists
	if err := os.MkdirAll(filepath.Dir(constant.DefaultYuanRongCurrentMasterInfoPath), dirMode); err != nil {
		return nil, err
	}

	// DO NOT CLOSE THIS FILE
	deployStdFile, err := os.OpenFile(filepath.Join(yrOpts.deployPath, "deploy_std.log"),
		os.O_RDWR|os.O_CREATE|os.O_TRUNC, fileMode)
	if err != nil {
		return nil, err
	}

	return deployStdFile, nil
}

func yrDeployMaster(path string, dumpPolicy currentMasterInfoDumpPolicy) error {
	var err error
	err, masterDeployCmd := concatMasterDeployCmd(path)
	if err != nil {
		return err
	}

	deployStdFile, err := prepareDeployWorkingDir()
	if err != nil {
		return err
	}
	masterDeployCmd.Stdout = deployStdFile
	masterDeployCmd.Stderr = deployStdFile

	masterRunningCtx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if yrOpts.verbose > 1 {
		go redirectFileContentToStd(masterRunningCtx, deployStdFile)
	}

	// block, run the command directly
	// no block, run the command until master_info_output file generated
	if yrOpts.block {
		blockRun(masterDeployCmd, cancel)
	}

	var waitChan chan error
	if err, waitChan = utils.ExecCommandUntil(masterDeployCmd, untilMasterDeploySucceed,
		constant.DeployReadyCheckTimeoutS, yrOpts.block); err != nil {
		// wait some time so the std output will be redirect to the stdout
		time.Sleep(deployFailureWaitTime)
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut,
			fmt.Sprintf("maybe you can go check the deploy log file %s for further details", deployStdFile.Name()),
			"", ".\n")
		clearCmdProcess(masterDeployCmd)
		return err
	}

	if err := yrMakeSymbolLink(dumpPolicy); err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut,
			fmt.Sprintf("warn: make symbol link failed: %s", err.Error()), "", ".\n")
		return nil
	}

	if yrOpts.block {
		if err := printMasterStartedHelpMsg(); err != nil {
			return err
		}
		// when block, wait here
		if waitChan != nil {
			return <-waitChan
		}
		return errors.New("wait failed, maybe the yr start failed")
	}

	return printMasterStartedHelpMsg()
}

func concatMasterDeployCmd(path string) (error, *exec.Cmd) {
	var err error

	// make sure fresh deploy.sh script exists and valid
	deployScriptPath := filepath.Join(path, "deploy", "process", "deploy.sh")
	if _, err = os.Stat(deployScriptPath); err != nil {
		return errors.New(fmt.Sprintf("maybe check if you have installed yuanrong: %s(%s)",
			deployScriptPath, err.Error())), nil
	}

	execName, _ := utils.GetGOOSType()
	args := make([]string, 0)
	args = append(args, deployScriptPath)
	err, args = concatArgs(args)
	if err != nil {
		return err, nil
	}
	masterDeployCmd := exec.Command(execName, args...)
	return nil, masterDeployCmd
}

func blockRun(masterDeployCmd *exec.Cmd, cancel context.CancelFunc) {
	// when block, use pdeathsig to make sure when this command exit, send sigterm to the deploy.sh
	// when no blocked, will just run
	masterDeployCmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGTERM,
	}

	ch := make(chan os.Signal, 2)
	signal.Notify(ch, syscall.SIGINT, syscall.SIGKILL, syscall.SIGTERM)
	go func() {
		select {
		case <-ch:
			cancel()
			if err := os.RemoveAll(constant.DefaultYuanRongCurrentMasterInfoPath); err != nil {
				// do nothing
			}
			os.Exit(0)
		}
	}()
}

func yrProcessDeployArgs() {
	if yrOpts.deployPath == "" {
		yrOpts.deployPath = "/tmp/yr_sessions/" + time.Now().Format("20060102150405")
	}
	if yrOpts.masterInfoOutput == "" {
		yrOpts.masterInfoOutput = filepath.Join(yrOpts.deployPath, "master.info")
	}
}

func yrMakeDeployPathSymbolLink() error {
	if yrOpts.deployPath != constant.DefaultYuanRongDeployDir {
		if !utils.Exists(filepath.Dir(constant.DefaultYuanRongDeployDir)) {
			if err := os.MkdirAll(filepath.Dir(constant.DefaultYuanRongDeployDir), dirMode); err != nil {
				// do nothing
			}
		}
		if err := utils.CreateSymbolicLinkWithForce(yrOpts.deployPath, constant.DefaultYuanRongDeployDir); err != nil {
			return err
		}
	}
	return nil
}

func yrMakeSymbolLink(dumpPolicy currentMasterInfoDumpPolicy) error {
	if yrOpts.masterInfoOutput != constant.DefaultYuanRongCurrentMasterInfoPath {
		switch dumpPolicy {
		case dumpPolicyNothing:
		case dumpPolicyInvalid:
			return nil
		case dumpPolicyReplace:
			info, err := utils.GetMasterInfoString(constant.YuanRongCurrentMasterInfoPath)
			if err != nil {
				info = fmt.Sprintf("failed to get the master info due to %s", err.Error())
			}
			colorMap := []colorprint.StringColorInfo{
				{Str: fmt.Sprintf("NOTE: dumped yuanrong cluster info at %s is updated.\n",
					constant.YuanRongCurrentMasterInfoPath), Color: colorprint.KeywordColor},
				{Str: fmt.Sprintf("There seems an existing yuanrong cluster already running on your node with info:\n"+
					" %s\n",
					info), Color: colorprint.Colorless},
				{Str: "The new yuanrong proccesses is running now. The old yuanrong cluster processes will continue" +
					" running on the background until `yr stop` is called\n", Color: colorprint.KeywordColor},
			}
			colorprint.PrintKeywords(yrOpts.cmdIO.Out, colorMap)
			fallthrough
		default:
			if err := utils.CreateSymbolicLinkWithForce(yrOpts.masterInfoOutput,
				constant.DefaultYuanRongCurrentMasterInfoPath); err != nil {
				return err
			}
		}
	}

	return nil
}

func clearCmdProcess(cmd *exec.Cmd) {
	if cmd.Process == nil {
		return
	}
	if err := cmd.Process.Kill(); err != nil {
		// DO NOTHING
	}
}

func untilMasterDeploySucceed(ctx context.Context, block bool) error {
	for {
		_, err := os.Stat(filepath.Join(yrOpts.deployPath, "master_pid_port.txt"))
		if err != nil && os.IsNotExist(err) {
			time.Sleep(time.Millisecond * constant.DeployReadyCheckIntervalMs)
			continue
		}
		_, err = os.Stat(yrOpts.masterInfoOutput)
		if err != nil && os.IsNotExist(err) {
			time.Sleep(time.Millisecond * constant.DeployReadyCheckIntervalMs)
			continue
		}
		if err == nil {
			return untilAgentDeploySucceed(ctx, false) // make sure agent is start successfully
		}
	}
}

func untilAgentDeploySucceed(ctx context.Context, block bool) error {
	for {
		_, err := os.Stat(filepath.Join(yrOpts.deployPath, "deploy_std.log"))
		if err != nil && os.IsNotExist(err) {
			time.Sleep(time.Millisecond * constant.DeployReadyCheckIntervalMs)
			continue
		}
		break
	}

	file, err := os.Open(filepath.Join(yrOpts.deployPath, "deploy_std.log"))
	if err != nil {
		return errors.New("failed to open deploy std file")
	}
	defer file.Close()

	for {
		scanner := bufio.NewScanner(file)
		for scanner.Scan() {
			line := scanner.Text()
			if !block && strings.Contains(line, "yuanrong data plane components installation") {
				return nil
			}
		}
	}
}

func yrDeployAgent(path string) error {
	agentScriptPath := filepath.Join(path, "deploy", "process", "deploy.sh")
	if _, err := os.Stat(agentScriptPath); err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.ErrOut, "Failed to start, ",
			fmt.Sprintf("%s not exists, check if you have installed yuanrong", agentScriptPath), ".\n")
		return err
	}

	execName, _ := utils.GetGOOSType()
	args := make([]string, 0)
	args = append(args, agentScriptPath)
	err, args := concatArgs(args)
	if err != nil {
		return err
	}
	agentDeployCmd := exec.Command(execName, args...)

	deployStdFile, err := prepareDeployWorkingDir()
	if err != nil {
		return err
	}
	agentDeployCmd.Stdout = deployStdFile
	agentDeployCmd.Stderr = deployStdFile

	if yrOpts.block {
		// when block, use pdeathsig to make sure when this command exit, send sigterm to the deploy.sh
		// when no blocked, will just run
		agentDeployCmd.SysProcAttr = &syscall.SysProcAttr{
			Pdeathsig: syscall.SIGTERM,
		}
	}

	if err, _ = utils.ExecCommandUntil(agentDeployCmd, untilAgentDeploySucceed,
		constant.DeployReadyCheckTimeoutS, yrOpts.block); err != nil {
		clearCmdProcess(agentDeployCmd)
		return err
	}

	return nil
}

func addIfNot(value, name, ignore string, result []string) []string {
	if value == ignore {
		return result
	}
	result = append(result, fmt.Sprintf("--%s", name))
	result = append(result, value)
	return result
}

func addIfNotAndAddMore(value, name, ignore, more string, result []string) []string {
	if value == ignore {
		return result
	}
	result = append(result, fmt.Sprintf("--%s", name))
	result = append(result, value)
	result = append(result, more)
	return result
}

func addIfNotBool(value bool, name string, ignore bool, result []string, noArg bool) []string {
	if value == ignore {
		return result
	}

	if noArg {
		if value {
			result = append(result, fmt.Sprintf("--%s", name))
		}
	} else {
		strValue := "false"
		if value {
			strValue = "true"
		}
		result = append(result, fmt.Sprintf("--%s", name))
		result = append(result, strValue)
	}
	return result
}

func concatArgs(result []string) (error, []string) {
	if yrOpts.ipAddress == "" {
		yrOpts.ipAddress = utils.GetLocalIP()
	}
	result = addIfNot(yrOpts.ipAddress, "ip_address", "", result)
	result = addIfNot(yrOpts.cpuNum, "cpu_num", "", result)
	result = addIfNot(yrOpts.deployPath, "deploy_path", "", result)
	result = addIfNot(yrOpts.memoryNum, "memory_num", "", result)
	result = addIfNot(yrOpts.sharedMemoryNum, "shared_memory_num", "", result)
	result = addIfNot(yrOpts.masterInfoOutput, "master_info_output", "", result)
	result = addIfNotBool(yrOpts.master, "master", false, result, true)
	result = addIfNot(yrOpts.masterInfo, "master_info", "", result)
	result = addIfNot(yrOpts.servicesPath, "services_path", "", result)
	result = addIfNot(yrOpts.masterIp, "master_ip", "", result)
	result = addIfNot(yrOpts.enableInheritEnv, "enable_inherit_env", "", result)
	// if use etcd_addr_list, must enable multi master
	result = addIfNotAndAddMore(yrOpts.etcdAddrList, "etcd_addr_list", "", "-e", result)

	result = addIfNot(yrOpts.dsNodeTimeoutS, "ds_node_timeout_s", "", result)
	result = addIfNot(yrOpts.dsNodeDeadTimeoutS, "ds_node_dead_timeout_s", "", result)
	result = addIfNot(yrOpts.dsHeartbeatIntervalMs, "ds_heartbeat_interval_ms", "", result)

	result = addIfNotBool(yrOpts.disableNcCheck, "disable_nc_check", false, result, true)

	restArgsIdx := 2
	result = append(result, os.Args[restArgsIdx:]...)

	for _, param := range result {
		if err := utils.ParamCheck(param); err != nil {
			return err, nil
		}
	}
	return nil, result
}
