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

// Package utils for common functions
package utils

import (
	"errors"
	"os"
	"path/filepath"
	"strings"

	"meta_service/common/reader"
)

const (
	defaultPath          = "/home/sn"
	defaultBinPath       = "/home/sn/bin"
	defaultConfigPath    = "/home/sn/config/config.json"
	defaultLogConfigPath = "/home/sn/config/log.json"
	DefaultFunctionPath  = "/home/sn/config/function.yaml"
)

// IsFile returns true if the path is a file
func IsFile(path string) bool {
	file, err := reader.ReadFileInfoWithTimeout(path)
	if err != nil {
		return false
	}
	return file.Mode().IsRegular()
}

// IsDir returns true if the path is a dir
func IsDir(path string) bool {
	dir, err := reader.ReadFileInfoWithTimeout(path)
	if err != nil {
		return false
	}

	return dir.IsDir()
}

// FileExists returns true if the path exists
func FileExists(path string) bool {
	_, err := reader.ReadFileInfoWithTimeout(path)
	if err != nil {
		return false
	}
	return true
}

// FileSize return path file size
func FileSize(path string) int64 {
	fileInfo, err := reader.ReadFileInfoWithTimeout(path)
	if err != nil {
		return 0
	}
	return fileInfo.Size()
}

// IsHexString judge If Hex String
func IsHexString(str string) bool {
	str = strings.ToLower(str)

	for _, c := range str {
		if c < '0' || (c > '9' && c < 'a') || c > 'f' {
			return false
		}
	}

	return true
}

// ValidateFilePath verify the legitimacy of the file path
func ValidateFilePath(path string) error {
	absPath, err := filepath.Abs(path)
	if err != nil || !strings.HasPrefix(path, absPath) {
		return errors.New("invalid file path, expect to be configured as an absolute path")
	}
	return nil
}

// GetBinPath get path of exec bin file
func GetBinPath() (string, error) {
	bin, err := os.Executable()
	if err != nil {
		return "", err
	}
	binPath := filepath.Dir(bin)
	return binPath, nil
}

// GetConfigPath get config.json file path
func GetConfigPath() (string, error) {
	binPath, err := GetBinPath()
	if err != nil {
		return "", err
	}
	if binPath == defaultBinPath {
		return defaultConfigPath, nil
	}
	return binPath + "/../config/config.json", nil
}

// GetFunctionConfigPath get function.yaml file path
func GetFunctionConfigPath() (string, error) {
	binPath, err := GetBinPath()
	if err != nil {
		return "", err
	}
	if binPath == defaultBinPath {
		return DefaultFunctionPath, nil
	}
	return binPath + "/../config/function.yaml", nil
}

// GetLogConfigPath get log.json file path
func GetLogConfigPath() (string, error) {
	binPath, err := GetBinPath()
	if err != nil {
		return "", err
	}
	if binPath == defaultBinPath {
		return defaultLogConfigPath, nil
	}
	return binPath + "/../config/log.json", nil
}

// GetDefaultPath get default path
func GetDefaultPath() (string, error) {
	binPath, err := GetBinPath()
	if err != nil {
		return "", err
	}
	if binPath == defaultBinPath {
		return defaultPath, nil
	}
	return binPath + "/..", nil
}
