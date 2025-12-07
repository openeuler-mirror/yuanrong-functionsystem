/*
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
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

// Package config is common logger client
package config

import (
	"encoding/json"
	"errors"
	"io/ioutil"
	"os"

	"meta_service/common/utils"

	"github.com/asaskevich/govalidator/v11"
)

const (
	fileMode = 0o750
)

// defaultCoreInfo default logger config
var defaultCoreInfo = CoreInfo{
	FilePath: "/home/sn/log",
	Level:    "INFO",
	Rolling: &RollingInfo{
		MaxSize:    400,  // Log file reaches maxSize should be compressed with unit MB
		MaxBackups: 1,    // Max compressed file nums
		MaxAge:     1,    // Max Age of old log files with unit days
		Compress:   true, // Determines if the log files should be compressed
	},
	Tick:       10,    // Unit: Second
	First:      10,    // Unit: Number of logs
	Thereafter: 5,     // Unit: Number of logs
	Tracing:    false, // tracing log switch
	Disable:    false, // Disable file logger
}

// RollingInfo contains log rolling configurations
type RollingInfo struct {
	MaxSize    int  `json:"maxsize" valid:",required"`
	MaxBackups int  `json:"maxbackups" valid:",required"`
	MaxAge     int  `json:"maxage" valid:",required"`
	Compress   bool `json:"compress" valid:",required"`
}

// CoreInfo contains the core info
type CoreInfo struct {
	Rolling    *RollingInfo `json:"rolling" valid:",optional"`
	FilePath   string       `json:"filepath" valid:",required"`
	Level      string       `json:"level" valid:",required"`
	Tick       int          `json:"tick" valid:"range(0|86400),optional"`
	First      int          `json:"first" valid:"range(0|20000),optional"`
	Thereafter int          `json:"thereafter" valid:"range(0|1000),optional"`
	Tracing    bool         `json:"tracing" valid:",optional"`
	Disable    bool         `json:"disable" valid:",optional"`
}

// GetCoreInfo get logger config by read log.json file
func GetCoreInfo(configFile string) (CoreInfo, error) {
	var info CoreInfo
	data, err := ioutil.ReadFile(configFile)
	if os.IsNotExist(err) {
		return defaultCoreInfo, nil
	}
	if err != nil {
		return defaultCoreInfo, err
	}
	err = json.Unmarshal(data, &info)
	if err != nil {
		return defaultCoreInfo, err
	}
	// if file path is empty return error
	// if log file is not writable
	// zap will create a new file with file path and file name
	if info.FilePath == "" {
		return defaultCoreInfo, errors.New("log file path is empty")
	}
	if _, err := govalidator.ValidateStruct(info); err != nil {
		return defaultCoreInfo, err
	}
	if err := utils.ValidateFilePath(info.FilePath); err != nil {
		return defaultCoreInfo, err
	}
	if err := os.MkdirAll(info.FilePath, fileMode); err != nil && !os.IsExist(err) {
		return defaultCoreInfo, err
	}

	return info, nil
}

// GetCoreInfoByParam get logger config by read log.json file
func GetCoreInfoByParam(configFile string) (CoreInfo, error) {
	var coreInfo CoreInfo
	data, err := ioutil.ReadFile(configFile)
	if os.IsNotExist(err) {
		return defaultCoreInfo, nil
	}
	if err != nil {
		return defaultCoreInfo, err
	}
	err = json.Unmarshal(data, &coreInfo)
	if err != nil {
		return defaultCoreInfo, err
	}
	// if file path is empty return error
	// if log file is not writable
	// zap will create a new file with file path and file name
	if coreInfo.FilePath == "" {
		return defaultCoreInfo, errors.New("log file path is empty")
	}
	if _, err := govalidator.ValidateStruct(coreInfo); err != nil {
		return defaultCoreInfo, err
	}
	if err := utils.ValidateFilePath(coreInfo.FilePath); err != nil {
		return defaultCoreInfo, err
	}
	if err := os.MkdirAll(coreInfo.FilePath, fileMode); err != nil && !os.IsExist(err) {
		return defaultCoreInfo, err
	}

	return coreInfo, nil
}

// GetDefaultCoreInfo get defaultCoreInfo
func GetDefaultCoreInfo() CoreInfo {
	return defaultCoreInfo
}
