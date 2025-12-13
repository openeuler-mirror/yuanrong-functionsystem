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

package clusterctrl

import (
	"fmt"
	"io"
	"net"
	"os/exec"
	"strings"

	"cli/utils"
)

func isLocalIP(ip string) bool {
	targetIP := net.ParseIP(ip)
	if targetIP == nil {
		return false
	}

	if targetIP.IsLoopback() || targetIP.IsLinkLocalUnicast() || targetIP.IsLinkLocalMulticast() {
		return true
	}

	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return false
	}

	for _, addr := range addrs {
		ipNet, _, err := net.ParseCIDR(addr.String())
		if err != nil {
			continue
		}
		if targetIP.Equal(ipNet) {
			return true
		}
	}
	return false
}

// SSHRemoteExecutor can execute commands on remote hosts with ssh
type SSHRemoteExecutor struct {
	baseRemoteExecutor
	sshCmdTemplate string // the ssh command
}

// NewSSHRemoteExecutor -
func NewSSHRemoteExecutor(sshCmdTemplate string) RemoteExecutor {
	se := &SSHRemoteExecutor{
		sshCmdTemplate: sshCmdTemplate,
	}
	se.Impl = se
	return se
}

// prepareCommand makes the ssh command
func (se *SSHRemoteExecutor) prepareCommand(host, command string) *exec.Cmd {
	result := se.sshCmdTemplate
	result = strings.ReplaceAll(result, utils.PlaceholderTarget, host)
	result = strings.ReplaceAll(result, utils.PlaceholderCommand, command)
	remoteCommand := strings.Fields(result)
	return exec.Command(remoteCommand[0], remoteCommand[1:]...)
}

// RunCommandWithPipe -
func (se *SSHRemoteExecutor) RunCommandWithPipe(host string, command string,
	env []string,
) (io.ReadCloser, func() error, error) {
	commandWithEnv := ""
	for _, e := range env {
		commandWithEnv += " export " + e + " && "
	}
	commandWithEnv += command
	remoteCommand := se.prepareCommand(host, commandWithEnv)
	if isLocalIP(host) {
		remoteCommand = exec.Command("/bin/sh", "-c", command)
	}

	remoteCommand.Env = append(remoteCommand.Env, env...)
	outPipe, err := remoteCommand.StdoutPipe()
	if err != nil {
		return nil, nil, fmt.Errorf("SSH remote executor failed to get stdout pipe: %v", err)
	}
	remoteCommand.Stderr = remoteCommand.Stdout
	if err := remoteCommand.Start(); err != nil {
		if closeErr := outPipe.Close(); closeErr != nil {
			// pass
		}
		return nil, nil, fmt.Errorf("SSH remote executor failed to start command on %s: %v", host, err)
	}
	waitFn := func() error {
		return remoteCommand.Wait()
	}
	return outPipe, waitFn, nil
}
