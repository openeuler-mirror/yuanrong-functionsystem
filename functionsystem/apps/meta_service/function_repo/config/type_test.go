/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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

package config

import (
	"encoding/json"
	"io/ioutil"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"

	"meta_service/common/etcd3"
)

func TestInitConfigFailed(t *testing.T) {
	convey.Convey("Test GetFunctionConfig", t, func() {
		convey.Convey("ReadFileFailed", func() {
			err := InitConfig("test")
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("UnmarshalFile", func() {
			patches := gomonkey.NewPatches()
			defer patches.Reset()
			patches.ApplyFunc(ioutil.ReadFile, func(filename string) ([]byte, error) { return []byte("00"), nil })
			err := InitConfig("test")
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("validateStruct", func() {
			patches := gomonkey.NewPatches()
			defer patches.Reset()
			RepoCfg = &Configs{
				EtcdCfg: etcd3.EtcdConfig{SslEnable: false},
				FunctionCfg: funcConfig{
					DefaultCfg: funcDefault{Timeout: 1},
				},
			}
			data, _ := json.Marshal(RepoCfg)
			patches.ApplyFunc(ioutil.ReadFile, func(filename string) ([]byte, error) { return data, nil })
			err := InitConfig("test")
			convey.ShouldNotBeNil(err)
		})
	})
}
