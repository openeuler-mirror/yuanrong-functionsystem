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

package utils

import (
	"fmt"
	"net"
	"os"
	"strings"
	"time"

	"gopkg.in/yaml.v3"
)

const (
	// PlaceholderTarget is the target host
	PlaceholderTarget = "{{target}}"
	// PlaceholderCommand is the target command used in ssh
	PlaceholderCommand = "{{command}}"
	// PlaceholderMasterInfo is the master info used in agent start command
	PlaceholderMasterInfo = "{{master_info}}"
	// PlaceholderClusterName is the master info used in stop command
	PlaceholderClusterName = "{{cluster_id}}"
)

// ClusterConfig Struct for cluster configuration
type ClusterConfig struct {
	ClusterName string   `yaml:"cluster_name"`
	MasterIp    []string `yaml:"master_ip"`
	AgentIp     []string `yaml:"agent_ip"`
	MasterCmd   []string `yaml:"master_cmd,omitempty"`
	AgentCmd    []string `yaml:"agent_cmd,omitempty"`
	StopCmd     []string `yaml:"stop_cmd,omitempty"`
	SSHCmd      string   `yaml:"ssh_cmd,omitempty"`
	Env         []string `yaml:"environment,omitempty"`
}

// NewDefaultClusterConfig make a config with default configs
func NewDefaultClusterConfig() *ClusterConfig {
	return &ClusterConfig{
		ClusterName: time.Now().Format("20060102T150405"),
		MasterCmd:   []string{"yr start --master"},
		AgentCmd:    []string{fmt.Sprintf("yr start --master_info=%s", PlaceholderMasterInfo)},
		StopCmd:     []string{fmt.Sprintf("yr stop --cluster_name=%s", PlaceholderClusterName)},
		SSHCmd:      fmt.Sprintf("ssh -q -n %s %s", PlaceholderTarget, PlaceholderCommand),
	}
}

// LoadClusterConfig Load the cluster configuration YAML file
func LoadClusterConfig(filename string) (*ClusterConfig, error) {
	data, err := os.ReadFile(filename)
	if err != nil {
		return nil, fmt.Errorf("failed to read file: %v", err)
	}

	clusterConfig := NewDefaultClusterConfig()
	if err := yaml.Unmarshal(data, clusterConfig); err != nil {
		return nil, fmt.Errorf("failed to parse YAML: %v", err)
	}

	if err := ValidateClusterConfig(*clusterConfig); err != nil {
		return nil, err
	}

	return clusterConfig, nil
}

// ValidateClusterConfig Validate the cluster configuration struct
func ValidateClusterConfig(clusterConfig ClusterConfig) error {
	// Validate required fields
	if clusterConfig.ClusterName == "" {
		return fmt.Errorf("field 'cluster_name' is required")
	}
	if len(clusterConfig.MasterIp) != 1 {
		errMsg := "too many master_ip"
		if len(clusterConfig.MasterIp) < 1 {
			errMsg = "no master_ip provided"
		}
		return fmt.Errorf(errMsg)
	}
	if len(clusterConfig.AgentIp) == 0 {
		return fmt.Errorf("field 'agent_ip' is required")
	}

	// Validate ip address
	for _, ip := range clusterConfig.MasterIp {
		if net.ParseIP(ip).To4() == nil {
			return fmt.Errorf("invalid IP address in master_ip: %s", ip)
		}
	}
	for _, ip := range clusterConfig.AgentIp {
		if net.ParseIP(ip).To4() == nil {
			return fmt.Errorf("invalid IP address in agent_ip: %s", ip)
		}
	}

	return nil
}

// ReplaceClusterConfigPlaceholderInAllCommands -
func ReplaceClusterConfigPlaceholderInAllCommands(commands []string, placeholder, value string) []string {
	var replacedCommands []string
	for _, cmd := range commands {
		replacedCommands = append(replacedCommands, strings.ReplaceAll(cmd, placeholder, value))
	}
	return replacedCommands
}
