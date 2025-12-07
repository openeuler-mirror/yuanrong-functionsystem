/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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

// Package versions for version info
package versions

var (
	// BuildVersion build version
	buildVersion = "UnKnown"
	// GitBranch git branch
	gitBranch = "Unknown"
	// GitHash git hash
	gitHash = "UnKnown"
)

// GetBuildVersion get build version
func GetBuildVersion() string {
	return buildVersion
}

// GetGitBranch get git branch
func GetGitBranch() string {
	return gitBranch
}

// GetGitHash get git hash
func GetGitHash() string {
	return gitHash
}
