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
	"net"
	"os"
	"testing"

	"meta_service/common/constants"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"
)

const (
	DefaultTimeout = 900
)

// TestDomain2IP convert domain to ip
// If this test case fails, the problem is caused by inline optimization of the Go compiler.
// go test add "-gcflags="all=-N -l",the case will pass
func TestDomain2IP(t *testing.T) {
	patches := [...]*gomonkey.Patches{
		gomonkey.ApplyFunc(net.LookupHost, func(_ string) ([]string, error) {
			return []string{"1.1.1.1"}, nil
		}),
	}
	defer func() {
		for idx := range patches {
			patches[idx].Reset()
		}
	}()

	type args struct {
		endpoint string
	}
	tests := []struct {
		args    args
		want    string
		wantErr bool
	}{
		{
			args{endpoint: "1.1.1.1:9000"},
			"1.1.1.1:9000",
			false,
		},
		{
			args{endpoint: "1.1.1.1"},
			"1.1.1.1",
			false,
		},
		{
			args{endpoint: "test:9000"},
			"1.1.1.1:9000",
			false,
		},
		{
			args{endpoint: "test"},
			"1.1.1.1",
			false,
		},
	}
	for _, tt := range tests {
		got, err := Domain2IP(tt.args.endpoint)
		if (err != nil) != tt.wantErr {
			t.Errorf("Domain2IP() error = %v, wantErr %v", err, tt.wantErr)
			return
		}
		if got != tt.want {
			t.Errorf("Domain2IP() got = %v, want %v", got, tt.want)
		}
	}
}

func TestFloat64ToByte(t *testing.T) {
	value := 123.45
	bytesValue := Float64ToByte(value)
	if ByteToFloat64(bytesValue) != value {
		t.Errorf("Float64ToByte and ByteToFloat64 failed")
	}

	bytes := []byte{'a'}
	ByteToFloat64(bytes)
}

func TestGetSystemMemoryUsed(t *testing.T) {
	if GetSystemMemoryUsed() == 0 {
		t.Log("GetSystemMemoryUsed is zero")
	}
}

func TestExistPath(t *testing.T) {
	path := os.Args[0]
	if !ExistPath(path) {
		t.Errorf("test path exist true failed, path: %s", path)
	}
	if ExistPath(path + "abc") {
		t.Errorf("test path exist false failed, path: %s", path+"abc")
	}
}

func TestFileSize(t *testing.T) {
	ret := FileSize("test/file")
	assert.Equal(t, ret, int64(0))
}

func TestIsDataSystemEnable(t *testing.T) {
	ret := IsDataSystemEnable()
	assert.Equal(t, ret, false)

	os.Setenv(constants.DataSystemBranchEnvKey, "t")
	ret = IsDataSystemEnable()
	assert.Equal(t, ret, true)
}

func TestUniqueID(t *testing.T) {
	uuid1 := UniqueID()
	uuid2 := UniqueID()
	assert.NotEqual(t, uuid1, uuid2)
}

func TestDeepCopy(t *testing.T) {
	srcString := ""
	copyString := DeepCopy(srcString)
	assert.Equal(t, nil, copyString)

	srcSlice := make([]int, 3)
	copySlice := DeepCopy(srcSlice)
	assert.Equal(t, 3, len(copySlice.([]int)))

	srcMap := make(map[int]int)
	srcMap[0] = 1
	copyMap := DeepCopy(srcMap)
	assert.Equal(t, 1, len(copyMap.(map[int]int)))
}

func TestValidateTimeout(t *testing.T) {
	var timeout int64
	timeout = 0
	ValidateTimeout(&timeout, DefaultTimeout)
	assert.Equal(t, int64(DefaultTimeout), timeout)

	timeout = maxTimeout + 1
	ValidateTimeout(&timeout, DefaultTimeout)
	assert.Equal(t, int64(maxTimeout), timeout)
}

func TestAzEnv(t *testing.T) {
	azString := AzEnv()
	assert.Equal(t, "defaultaz", azString)
}

func TestClearByteMemory(t *testing.T) {
	ClearByteMemory([]byte{'a'})
}
