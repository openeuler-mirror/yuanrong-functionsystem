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
	"bufio"
	"encoding/binary"
	"io"
	"math"
	"meta_service/common/constants"
	"meta_service/common/reader"
	"net"
	"os"
	"reflect"
	"strconv"
	"strings"
	"unsafe"

	"github.com/pborman/uuid"
)

const (
	// OriginDefaultTimeout is 900
	OriginDefaultTimeout = 900
	// maxTimeout is 100 days
	maxTimeout        = 100 * 24 * 3600
	bytesToMb         = 1024 * 1024
	uint64ArrayLength = 8
)

// AzEnv set defaultaz env
func AzEnv() string {
	az := os.Getenv(constants.ZoneKey)
	if az == "" {
		az = constants.DefaultAZ
	}
	if len(az) > constants.ZoneNameLen {
		az = az[0 : constants.ZoneNameLen-1]
	}
	return az
}

// Domain2IP convert domain to ip
func Domain2IP(endpoint string) (string, error) {
	var host, port string
	var err error
	host = endpoint
	if strings.Contains(endpoint, ":") {
		host, port, err = net.SplitHostPort(endpoint)
		if err != nil {
			return "", err
		}
	}
	if net.ParseIP(host) != nil {
		return endpoint, nil
	}
	ips, err := net.LookupHost(host)
	if err != nil {
		return "", err
	}
	if port == "" {
		return ips[0], nil
	}
	return net.JoinHostPort(ips[0], port), nil
}

// DeepCopy will generate a new copy of original collection type
// currently this function is not recursive so elements will not be deep copied
func DeepCopy(origin interface{}) interface{} {
	oriTyp := reflect.TypeOf(origin)
	oriVal := reflect.ValueOf(origin)
	switch oriTyp.Kind() {
	case reflect.Slice:
		elemType := oriTyp.Elem()
		length := oriVal.Len()
		capacity := oriVal.Cap()
		newObj := reflect.MakeSlice(reflect.SliceOf(elemType), length, capacity)
		reflect.Copy(newObj, oriVal)
		return newObj.Interface()
	case reflect.Map:
		newObj := reflect.MakeMapWithSize(oriTyp, len(oriVal.MapKeys()))
		for _, key := range oriVal.MapKeys() {
			value := oriVal.MapIndex(key)
			newObj.SetMapIndex(key, value)
		}
		return newObj.Interface()
	default:
		return nil
	}
}

// ValidateTimeout check timeout
func ValidateTimeout(timeout *int64, defaultTimeout int64) {
	if *timeout <= 0 {
		*timeout = defaultTimeout
		return
	}
	*timeout = *timeout + defaultTimeout - OriginDefaultTimeout
	if *timeout > maxTimeout {
		*timeout = maxTimeout
	}
}

// IsDataSystemEnable return the datasystem enable flag
func IsDataSystemEnable() bool {
	branch, err := strconv.ParseBool(os.Getenv(constants.DataSystemBranchEnvKey))
	if err != nil {
		branch = false
	}
	return branch
}

// ClearStringMemory -
func ClearStringMemory(s string) {
	bs := *(*[]byte)(unsafe.Pointer(&s))
	ClearByteMemory(bs)
}

// ClearByteMemory -
func ClearByteMemory(b []byte) {
	for i := 0; i < len(b); i++ {
		b[i] = 0
	}
}

// Float64ToByte -
func Float64ToByte(float float64) []byte {
	bits := math.Float64bits(float)
	bytes := make([]byte, 8)
	binary.LittleEndian.PutUint64(bytes, bits)
	return bytes
}

// ByteToFloat64 -
func ByteToFloat64(bytes []byte) float64 {
	// bounds check to guarantee safety of function Uint64
	if len(bytes) != uint64ArrayLength {
		return 0
	}
	bits := binary.LittleEndian.Uint64(bytes)
	return math.Float64frombits(bits)
}

// GetSystemMemoryUsed -
func GetSystemMemoryUsed() float64 {
	srcFile, err := os.Open("/sys/fs/cgroup/memory/memory.stat")
	if err != nil {
		return 0
	}
	defer srcFile.Close()

	reader := bufio.NewReader(srcFile)
	for {
		lineBytes, _, err := reader.ReadLine()
		if err == io.EOF {
			break
		}

		lineStr := string(lineBytes)
		if strings.Contains(lineStr, "rss ") {
			rssStr := strings.TrimPrefix(lineStr, "rss ")
			rssStr = strings.Trim(rssStr, "\n")

			value, err := strconv.ParseInt(rssStr, 10, 64)
			if err != nil {
				break
			}
			return float64(value) / bytesToMb
		}
	}
	return 0
}

// ExistPath whether path exists
func ExistPath(path string) bool {
	_, err := reader.ReadFileInfoWithTimeout(path)
	if err != nil && os.IsNotExist(err) {
		return false
	}
	return true
}

// UniqueID get unique ID
func UniqueID() string {
	return uuid.NewRandom().String()
}
