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

package utils

import (
	"encoding/json"
	"os"
	"strings"
	"time"

	common "meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/common/urnutils"
	"meta_service/function_repo/model"
	"meta_service/function_repo/utils/constants"
)

const (
	// layerURNLen layer URN split len is 7
	layerURNLen = 7
	// functionNameStartIndex for serviceID function
	functionNameStartIndex = 2
)

// GetUTCRevisionID gets utc revision id.
func GetUTCRevisionID() string {
	return strings.Replace(time.Now().UTC().Format("20060102150405.999"), ".", "", -1)
}

// GetDefaultVersion return Default version number of the function
func GetDefaultVersion() string {
	return constants.DefaultVersion
}

// GetDefaultFaaSVersion return Default version number of the function
func GetDefaultFaaSVersion() string {
	return common.DefaultLatestFaaSVersion
}

// InterceptNameAndVersionFromLayerURN the layer name and version number in the URN are separated.
func InterceptNameAndVersionFromLayerURN(l string) (string, string) {
	// sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:layer:aa:1
	n := strings.Split(l, ":")
	// layer URN split len is 7
	if len(n) >= layerURNLen {
		return n[len(n)-2], n[len(n)-1]
	}
	return "", ""
}

// RemoveServiceID Remove serviceID from the function name.
func RemoveServiceID(n string) string {
	if strings.HasPrefix(n, urnutils.ServicePrefix) {
		split := strings.Split(n, urnutils.DefaultSeparator)
		// the function name starts from the second position.
		return strings.Join(split[functionNameStartIndex:], urnutils.DefaultSeparator)
	}
	return n
}

// IsLatestVersion checks whether the input version is the default version.If the value is null,the value is also true.
func IsLatestVersion(v string) bool {
	return v == "" || v == GetDefaultVersion() || v == GetDefaultFaaSVersion()
}

// EncryptETCDValue encode the the etcd value
func EncryptETCDValue(text string) (string, error) {
	return text, nil
}

// DecryptETCDValue decode the etcd value
func DecryptETCDValue(text string) (string, error) {
	return text, nil
}

// EncodeEnv encode the environment
func EncodeEnv(m map[string]string) (string, error) {
	envByte, err := json.Marshal(m)
	if err != nil {
		return "", err
	}
	env, err := EncryptETCDValue(string(envByte))
	if err != nil {
		log.GetLogger().Errorf("failed to encode ETCD info, error:%s", err.Error())
		return "", err
	}
	return env, nil
}

// DecodeEnv decode the environment
func DecodeEnv(s string) (map[string]string, error) {
	env := make(map[string]string, common.DefaultMapSize)
	if len(s) != 0 {
		value, err := DecryptETCDValue(s)
		if err != nil {
			log.GetLogger().Errorf("failed to decrypt ETCD info, error:%s", err)
			return nil, err
		}
		err = json.Unmarshal([]byte(value), &env)
		if err != nil {
			log.GetLogger().Errorf("failed to unmarshal env, error: %s", err)
			return nil, err
		}
	}
	return env, nil
}

// MetadataPrefix returns the prefix of function metadata in ETCD.
func MetadataPrefix() string {
	return constants.MetadataPrefix + os.Getenv("METADATA_PREFIX")
}

// NowTimeF returns the formatted UTC time.
func NowTimeF() string {
	return time.Now().UTC().Format("2006-01-02 15:04:05.000 MST")
}

// GetPoolLabels -
func GetPoolLabels(p []model.ResourceAffinitySelector) string {
	var groups []string
	for _, policy := range p {
		groups = append(groups, policy.Group)
	}
	result := strings.Join(groups, ",")
	return result
}

// GetRuntimeName compatible for java8 and java1.8
func GetRuntimeName(kind, runtime string) string {
	if kind == common.Faas {
		if runtime == common.DefaultJavaRuntimeName {
			return common.DefaultJavaRuntimeNameForFaas
		}
	} else {
		if runtime == common.DefaultJavaRuntimeNameForFaas {
			return common.DefaultJavaRuntimeName
		}
	}
	return runtime
}
