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

	"cli/utils"
)

// Exec a command on master IP
func Exec(cfg *utils.ClusterConfig, executor RemoteExecutor, execCommand string) error {
	if len(cfg.MasterIp) != 1 {
		return errors.New("only accept one master ip")
	}
	stream, execErr := executor.RunCommandStream(cfg.MasterIp[0], execCommand, cfg.Env)
	if execErr != nil {
		return execErr
	}
	if stream == nil {
		return errors.New("unexpected nil pointer of the output stream when exec command")
	}
	finished := false
	for finished == false {
		select {
		case r, ok := <-stream:
			finished = !ok
			if finished {
				return nil
			}
			if r.Error != nil {
				return r.Error
			}
			fmt.Printf("%s", r.Output)
		}
	}
	return nil
}
