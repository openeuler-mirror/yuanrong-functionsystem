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
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"
)

func TestInitConfig(t *testing.T) {
	convey.Convey("TestInitConfig", t, func() {
		convey.Convey("test 1", func() {
			resourcePath := os.Getenv("ResourcePath")
			coreInfo, err := GetCoreInfo(filepath.Join(resourcePath, "../log.json"))
			fmt.Printf("log config:%+v\n", coreInfo)
			convey.So(err, convey.ShouldEqual, nil)
		})
	})
}

func TestInitConfigWithReadFileError(t *testing.T) {
	convey.Convey("TestInitConfigWithEmptyPath", t, func() {
		convey.Convey("test 1", func() {
			patches := [...]*gomonkey.Patches{
				gomonkey.ApplyFunc(ioutil.ReadFile,
					func(filename string) ([]byte, error) {
						return nil, errors.New("mock read file error")
					}),
			}
			defer func() {
				for _, p := range patches {
					p.Reset()
				}
			}()
			coreInfo, err := GetCoreInfo("/home/sn/config/log.json")
			fmt.Printf("error:%s\n", err)
			fmt.Printf("log config:%+v\n", coreInfo)
			convey.So(err, convey.ShouldNotEqual, nil)
		})
	})
}

func TestInitConfigWithErrorJson(t *testing.T) {
	convey.Convey("TestInitConfigWithEmptyPath", t, func() {
		convey.Convey("test 1", func() {
			mockErrorJson := "{\n\"filepath\": \"/home/sn/mock\",\n\"level\": \"INFO\",\n\"maxsize\": " +
				"500,\n\"maxbackups\": 1,\n\"maxage\": 1,\n\"compress\": true\n"
			patches := [...]*gomonkey.Patches{
				gomonkey.ApplyFunc(ioutil.ReadFile,
					func(filename string) ([]byte, error) {
						return []byte(mockErrorJson), nil
					}),
			}
			defer func() {
				for _, p := range patches {
					p.Reset()
				}
			}()
			coreInfo, err := GetCoreInfo("/home/sn/config/log.json")
			fmt.Printf("error:%s\n", err)
			fmt.Printf("log config:%+v\n", coreInfo)
			convey.So(err, convey.ShouldNotEqual, nil)
		})
	})
}

func TestInitConfigWithEmptyPath(t *testing.T) {
	convey.Convey("TestInitConfigWithEmptyPath", t, func() {
		convey.Convey("test 1", func() {
			mockCfgInfo := "{\n\"filepath\": \"\",\n\"level\": \"INFO\",\n\"maxsize\": " +
				"500,\n\"maxbackups\": 1,\n\"maxage\": 1,\n\"compress\": true\n}"
			patches := [...]*gomonkey.Patches{
				gomonkey.ApplyFunc(ioutil.ReadFile,
					func(filename string) ([]byte, error) {
						return []byte(mockCfgInfo), nil
					}),
			}
			defer func() {
				for _, p := range patches {
					p.Reset()
				}
			}()
			coreInfo, err := GetCoreInfo("/home/sn/config/log.json")
			fmt.Printf("error:%s\n", err)
			fmt.Printf("log config:%+v\n", coreInfo)
			convey.So(err, convey.ShouldNotEqual, nil)
		})
	})
}

func TestInitConfigWithValidateError(t *testing.T) {
	convey.Convey("TestInitConfigWithEmptyPath", t, func() {
		convey.Convey("test 1", func() {
			mockErrorJson := "{\n\"filepath\": \"some_relative_path\",\n\"level\": \"INFO\",\n\"maxsize\": " +
				"500,\n\"maxbackups\": 1,\n\"maxage\": 1}"
			patches := [...]*gomonkey.Patches{
				gomonkey.ApplyFunc(ioutil.ReadFile,
					func(filename string) ([]byte, error) {
						return []byte(mockErrorJson), nil
					}),
			}
			defer func() {
				for _, p := range patches {
					p.Reset()
				}
			}()
			coreInfo, err := GetCoreInfo("/home/sn/config/log.json")
			fmt.Printf("error:%s\n", err)
			fmt.Printf("log config:%+v\n", coreInfo)
			convey.So(err, convey.ShouldNotEqual, nil)
		})
	})
}

func TestGetDefaultCoreInfo(t *testing.T) {
	tests := []struct {
		name string
		want CoreInfo
	}{
		{
			name: "test001",
			want: CoreInfo{
				FilePath: "/home/sn/log",
				Level:    "INFO",
				Rolling: &RollingInfo{
					MaxSize:    400,
					MaxBackups: 1,
					MaxAge:     1,
					Compress:   true,
				},
				Tick:       10,    // Unit: Second
				First:      10,    // Unit: Number of logs
				Thereafter: 5,     // Unit: Number of logs
				Tracing:    false, // tracing log switch
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := GetDefaultCoreInfo(); !reflect.DeepEqual(got, tt.want) {
				t.Errorf("GetDefaultCoreInfo() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestGetCoreInfoByParam(t *testing.T) {
	GetCoreInfoByParam("/home/sn/config/log.json")

	patch1 := gomonkey.ApplyFunc(ioutil.ReadFile, func(string) ([]byte, error) {
		return nil, errors.New("test")
	})
	GetCoreInfoByParam("")
	patch1.Reset()

	patch2 := gomonkey.ApplyFunc(ioutil.ReadFile, func(string) ([]byte, error) {
		return nil, nil
	})
	GetCoreInfoByParam("")
	patch2.Reset()

	patch3 := gomonkey.ApplyFunc(ioutil.ReadFile, func(string) ([]byte, error) {
		return nil, nil
	})
	patch3.ApplyFunc(json.Unmarshal, func([]byte, interface{}) error {
		return nil
	})
	GetCoreInfoByParam("")
	patch3.Reset()
}
