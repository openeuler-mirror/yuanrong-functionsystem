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

package start

import (
	"cli/constant"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path"
	"runtime"
	"testing"
	"time"

	"cli/pkg/cmdio"
	"cli/utils"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
)

func prepareMockDeployDirAndShellScripts(yuanrongDir string, deployDir string) {
	infoContentStr := "master_ip:172.17.0.17,etcd_ip:172.17.0.17,etcd_port:13917,global_scheduler_port:14935,ds_master_port:15269,etcd_peer_port:18275,bus-proxy:27893,bus:24860,ds-worker:33615,"
	content := fmt.Sprintf(`#!/bin/bash
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --master_info_output) masterinfooutput="$2"; shift ;;
    esac
    shift
done
echo "%s" > "%s"
echo "%s" > "$masterinfooutput"
`,
		infoContentStr, path.Join(deployDir, "master_pid_port.txt"), infoContentStr)

	if err := os.MkdirAll(path.Join(yuanrongDir, "deploy", "process"), os.ModeDir); err != nil {
		fmt.Printf("failed to create mock deploy dir, err: %s\n", err.Error())
	}
	if err := os.MkdirAll(path.Dir(constant.DefaultYuanRongCurrentMasterInfoPath), os.ModeDir); err != nil {
		fmt.Printf("failed to create mock DefaultYuanRongCurrentMasterInfoPath dir, err: %s\n", err.Error())
	}
	if err := os.WriteFile(path.Join(yuanrongDir, "deploy", "process", "deploy.sh"), []byte(content),
		0644); err != nil {
		fmt.Printf("failed to write mock deploy shell, err: %s\n", err.Error())
	}
}

func clearMockDeployDirAndShellScripts(deployDir string) {
	_ = os.RemoveAll(deployDir)
}

type mockFileInfo struct{}

func (m *mockFileInfo) Name() string       { return "" }
func (m *mockFileInfo) Size() int64        { return 0 }
func (m *mockFileInfo) Mode() os.FileMode  { return 0 }
func (m *mockFileInfo) ModTime() time.Time { return time.Time{} }
func (m *mockFileInfo) IsDir() bool        { return false }
func (m *mockFileInfo) Sys() interface{}   { return nil }

type mockCmd struct {
	*exec.Cmd
	started bool
	waitErr error
}

func (m *mockCmd) Start() error {
	m.started = true
	return nil
}

func (m *mockCmd) Wait() error {
	return m.waitErr
}

func (m *mockCmd) ProcessState() *os.ProcessState {
	return &os.ProcessState{}
}

var (
	workdir, _ = os.Getwd()
)

func ATestYrStartYuanRong(t *testing.T) {
	Convey("Test yrStartYuanRong", t, func() {
		Convey("When system is supported and master is true, no error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			patches.ApplyFunc(yrDeployMaster, func(path string, _ currentMasterInfoDumpPolicy) error {
				return nil
			})

			yrOpts = yrStartOptions{
				master: true,
			}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldBeNil)
		})

		Convey("When system is supported and master_info is provided, no error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			patches.ApplyFunc(yrDeployAgent, func(path string) error {
				return nil
			})

			yrOpts = yrStartOptions{
				masterInfo: "some_master_info",
			}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldBeNil)
		})

		Convey("When system is not supported, an error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return false
			})
			defer patches.Reset()
			yrOpts = yrStartOptions{
				verbose: 1,
				cmdIO: &cmdio.CmdIO{
					Out:    os.Stdout,
					ErrOut: os.Stderr,
				},
			}
			err := yrStartYuanRong(nil, nil)
			So(err, ShouldNotBeNil)
			So(err.Error(), ShouldEqual, "unsupported os: "+runtime.GOOS)
		})

		Convey("When both master and master_info are provided, an error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			yrOpts = yrStartOptions{
				master:     true,
				masterInfo: "some_master_info",
			}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldNotBeNil)
			So(err.Error(), ShouldEqual, "only one of `master` and `master_info` can be specified")
		})

		Convey("When neither master nor master_info is provided, an error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			yrOpts = yrStartOptions{}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldNotBeNil)
			So(err.Error(), ShouldEqual, "one of `master` and `master_info` must be specified")
		})

		Convey("When yrDeployMaster returns an error, the error should be propagated", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			expectedError := errors.New("deploy master error")
			patches.ApplyFunc(yrDeployMaster, func(path string, _ currentMasterInfoDumpPolicy) error {
				return expectedError
			})

			yrOpts = yrStartOptions{
				master: true,
			}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldNotBeNil)
			So(err, ShouldEqual, expectedError)
		})

		Convey("When yrDeployAgent returns an error, the error should be propagated", func() {
			patches := gomonkey.ApplyFunc(utils.SupportSystem, func() bool {
				return true
			})
			defer patches.Reset()

			expectedError := errors.New("deploy agent error")
			patches.ApplyFunc(yrDeployAgent, func(path string) error {
				return expectedError
			})

			yrOpts = yrStartOptions{
				masterInfo: "some_master_info",
			}

			err := yrStartYuanRong(nil, nil)
			So(err, ShouldNotBeNil)
			So(err, ShouldEqual, expectedError)
		})
	})
}

func prepareMockFiles(files ...string) {
	for _, file := range files {
		_ = os.MkdirAll(path.Dir(file), os.ModeDir)
		_ = os.WriteFile(file, []byte{}, os.ModePerm)
	}
}

func removePreparedMockFiles(files ...string) {
	for _, file := range files {
		_ = os.RemoveAll(file)
	}
}

func removeSymbolLinks() {
	_ = os.RemoveAll(constant.DefaultYuanRongDeployDir)
	_ = os.RemoveAll(constant.DefaultYuanRongCurrentMasterInfoPath)
}

func TestYrDeployMaster(t *testing.T) {
	// mock files
	mockDeployPath := path.Join("/tmp", "deploy")
	mockDeployShellPath := path.Join(constant.YuanRongInstallationDir, "deploy", "process", "deploy.sh")
	mockMasterInfoOutputPath := path.Join(mockDeployPath, "master.info")
	mockMasterInfoPidOutputPath := path.Join(mockDeployPath, "master_pid_port.txt")

	// clear previous mock files
	removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
	clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
	removeSymbolLinks()

	Convey("Test yrDeployMaster", t, func() {
		Convey("When deploy.sh script exists and no existing master info, no error should be returned", func() {
			patches := gomonkey.ApplyFunc(utils.GetGOOSType, func() (string, string) {
				return "bash", "-c"
			})

			defer patches.Reset()

			yrOpts = yrStartOptions{
				verbose:          1,
				block:            false,
				deployPath:       mockDeployPath,
				masterInfoOutput: mockMasterInfoOutputPath,
				cmdIO: &cmdio.CmdIO{
					Out:    os.Stdout,
					ErrOut: os.Stderr,
				},
			}

			removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
			removeSymbolLinks()
			prepareMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			prepareMockDeployDirAndShellScripts(constant.YuanRongInstallationDir, mockDeployPath)

			err := yrDeployMaster(constant.YuanRongInstallationDir, dumpPolicyNew)
			So(err, ShouldBeNil)
		})

		Convey("When deploy.sh script does not exist, an error should be returned", func() {
			removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
			removeSymbolLinks()
			prepareMockFiles(mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)

			err := yrDeployMaster(constant.YuanRongInstallationDir, dumpPolicyNew)
			So(err, ShouldNotBeNil)
			So(err.Error(), ShouldContainSubstring, "maybe check if you have installed yuanrong")
		})

		Convey("When utils.ExecCommandUntil returns an error, the error should be propagated", func() {
			removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
			removeSymbolLinks()
			prepareMockFiles(mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			prepareMockDeployDirAndShellScripts(constant.YuanRongInstallationDir, mockDeployPath)

			patches := gomonkey.ApplyFunc(utils.GetGOOSType, func() (string, string) {
				return "bash", "-c"
			})
			expectedError := errors.New("exec command until error")
			patches.ApplyFunc(utils.ExecCommandUntil, func(cmd *exec.Cmd, check func(context.Context) error, timeout int) (error, chan error) {
				return expectedError, nil
			})

			patches.ApplyFunc(redirectFileContentToStd, func(ctx context.Context, deployStdFile *os.File) {
				return
			})
			defer patches.Reset()

			yrOpts = yrStartOptions{
				verbose:          1,
				block:            false,
				deployPath:       mockDeployPath,
				masterInfoOutput: mockMasterInfoPidOutputPath,
				cmdIO: &cmdio.CmdIO{
					Out:    os.Stdout,
					ErrOut: os.Stderr,
				},
			}

			err := yrDeployMaster(constant.YuanRongInstallationDir, dumpPolicyNew)
			So(err, ShouldNotBeNil)
			So(err, ShouldEqual, expectedError)
		})

		Convey("When yrMakeSymbolLink returns an error, the error should be propagated", func() {
			removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
			removeSymbolLinks()
			prepareMockFiles(mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			prepareMockDeployDirAndShellScripts(constant.YuanRongInstallationDir, mockDeployPath)

			expectedError := errors.New("make symbol link error")
			patches := gomonkey.ApplyFunc(utils.GetGOOSType, func() (string, string) {
				return "bash", "-c"
			}).ApplyFunc(utils.ExecCommandUntil, func(cmd *exec.Cmd, check func(context.Context) error, timeout int) (error, chan error) {
				return nil, nil
			}).ApplyFunc(redirectFileContentToStd, func(ctx context.Context, deployStdFile *os.File) {
				return
			}).ApplyFunc(yrMakeSymbolLink, func(dumpPolicy currentMasterInfoDumpPolicy) error {
				return expectedError
			})
			defer patches.Reset()

			yrOpts = yrStartOptions{
				verbose:          1,
				block:            false,
				deployPath:       mockDeployPath,
				masterInfoOutput: mockMasterInfoOutputPath,
				cmdIO: &cmdio.CmdIO{
					Out:    os.Stdout,
					ErrOut: os.Stderr,
				},
			}

			err := yrDeployMaster(constant.YuanRongInstallationDir, dumpPolicyNew)
			So(err, ShouldNotBeNil)
			So(err, ShouldEqual, expectedError)
		})

		Convey("When printMasterStartedHelpMsg returns an error, the error should be propagated", func() {
			removePreparedMockFiles(mockDeployShellPath, mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			clearMockDeployDirAndShellScripts(constant.YuanRongInstallationDir)
			removeSymbolLinks()
			prepareMockFiles(mockMasterInfoOutputPath, mockMasterInfoPidOutputPath)
			prepareMockDeployDirAndShellScripts(constant.YuanRongInstallationDir, mockDeployPath)

			patches := gomonkey.ApplyFunc(utils.GetGOOSType, func() (string, string) {
				return "bash", "-c"
			})
			defer patches.Reset()

			patches.ApplyFunc(yrMakeSymbolLink, func(dumpPolicy currentMasterInfoDumpPolicy) error {
				return nil
			})
			patches.ApplyFunc(redirectFileContentToStd, func(ctx context.Context, deployStdFile *os.File) {
				return
			})
			expectedError := errors.New("print master started help msg error")
			patches.ApplyFunc(printMasterStartedHelpMsg, func() error {
				return expectedError
			})

			yrOpts = yrStartOptions{
				verbose:          1,
				block:            false,
				deployPath:       mockDeployPath,
				masterInfoOutput: mockMasterInfoOutputPath,
				cmdIO: &cmdio.CmdIO{
					Out:    os.Stdout,
					ErrOut: os.Stderr,
				},
			}

			err := yrDeployMaster(constant.YuanRongInstallationDir, dumpPolicyNew)
			So(err, ShouldNotBeNil)
			So(err, ShouldEqual, expectedError)
		})
	})
}
