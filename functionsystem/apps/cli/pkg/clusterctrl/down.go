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
	"os"

	"cli/utils"
	"cli/utils/colorprint"
)

// Down the cluster
func Down(cfg *utils.ClusterConfig, executor RemoteExecutor) error {
	shutdownCommands := utils.ReplaceClusterConfigPlaceholderInAllCommands(cfg.StopCmd,
		utils.PlaceholderClusterName, cfg.ClusterName)
	_, err := executor.RunCommandsOnHostsParallelSync(cfg.AgentIp, shutdownCommands, cfg.Env)
	if err != nil {
		return err
	}
	colorprint.PrintSuccess(os.Stdout, "succeed ", "to stop all agents")
	if _, err := executor.RunCommandsSync(cfg.MasterIp[0], shutdownCommands, cfg.Env); err != nil {
		return err
	}
	colorprint.PrintSuccess(os.Stdout, "succeed ", "to stop master")
	return nil
}
