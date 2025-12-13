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
	"errors"
	"fmt"
	"os"
	"strings"

	"cli/constant"
	"cli/utils"
	"cli/utils/colorprint"
)

// Up a cluster
func Up(cfg *utils.ClusterConfig, executor RemoteExecutor) error {
	masterInfo, err := upMasterNode(cfg, executor)
	if err != nil {
		return err
	}
	colorprint.PrintSuccess(os.Stdout, "succeed ", fmt.Sprintf("to start up master node, master info: %s", masterInfo))
	cfg.Env = append(cfg.Env, fmt.Sprintf("YR_MASTER_INFO=%s", masterInfo))
	err = upAgentNode(cfg, executor, masterInfo)
	if err != nil {
		return err
	}
	colorprint.PrintSuccess(os.Stdout, "succeed ", fmt.Sprintf("to start up %d agent nodes.", len(cfg.AgentIp)))
	colorprint.PrintSuccess(os.Stdout, "succeed ", fmt.Sprintf("to up a yuanrong cluster."))
	return nil
}

func upMasterNode(cfg *utils.ClusterConfig, executor RemoteExecutor) (string, error) {
	if len(cfg.MasterIp) != 1 {
		return "", errors.New("only accept one master ip")
	}
	masterIP := cfg.MasterIp[0]

	out, err := executor.RunCommandsSync(masterIP, cfg.MasterCmd, cfg.Env)
	if err != nil {
		return "", err
	}
	masterInfo := ""
	splitOut := strings.Split(string(out), "\n")
	for i, v := range splitOut {
		if strings.Contains(v, "Cluster master info:") && i+1 < len(splitOut) {
			masterInfo = splitOut[i+1]
			masterInfo = strings.Trim(masterInfo, "\n")
			masterInfo = strings.Trim(masterInfo, "\t")
			masterInfo = strings.Trim(masterInfo, " ")
			return masterInfo, nil
		}
	}

	masterInfoByte, err := executor.RunCommandSync(masterIP, fmt.Sprintf("cat %s",
		constant.DefaultYuanRongCurrentMasterInfoPath), cfg.Env)
	if err != nil {
		return "", fmt.Errorf("failed to get master info: %w", err)
	}
	return strings.Trim(string(masterInfoByte), "\n"), nil
}

func upAgentNode(cfg *utils.ClusterConfig, executor RemoteExecutor, masterInfo string) error {
	agentCommands := utils.ReplaceClusterConfigPlaceholderInAllCommands(cfg.AgentCmd, utils.PlaceholderMasterInfo,
		masterInfo)
	_, err := executor.RunCommandsOnHostsParallelSync(cfg.AgentIp, agentCommands, cfg.Env)
	return err
}
