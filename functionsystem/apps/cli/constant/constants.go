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

// Package constant is constant of CLI
package constant

// Build input parameter variables
var (
	// CliName cli tool name
	CliName = "yr"
	// PlatformName service platform name
	PlatformName = "OpenYuanrong"
)

// Usage
const (
	// DeployReadyCheckIntervalMs -
	DeployReadyCheckIntervalMs = 500
	// DeployStdLogRedirectIntervalMs -
	DeployStdLogRedirectIntervalMs = 100
	// DeployReadyCheckTimeoutS -
	DeployReadyCheckTimeoutS = 120
)

const (
	// DefaultYuanRongInstallationDir is the default installation dir to yuanrong
	DefaultYuanRongInstallationDir = "/opt/yuanrong"
	// DefaultYuanRongDeployDir is the default deploy path
	DefaultYuanRongDeployDir = "/tmp/yr_sessions/latest"
	// DefaultYuanRongCurrentMasterInfoPath is the default master info path
	DefaultYuanRongCurrentMasterInfoPath = "/tmp/yr_sessions/yr_current_master_info"

	// EnvYuanRongInstallationDir -
	EnvYuanRongInstallationDir = "YR_INSTALLATION_DIR"
	// EnvYuanRongDeployDir -
	EnvYuanRongDeployDir = "YR_DEPLOY_DIR"
)

var (
	// YuanRongInstallationDir -
	YuanRongInstallationDir = DefaultYuanRongInstallationDir
	// YuanRongDeployDir -
	YuanRongDeployDir = DefaultYuanRongDeployDir
	// YuanRongCurrentMasterInfoPath -
	YuanRongCurrentMasterInfoPath = DefaultYuanRongCurrentMasterInfoPath
)
