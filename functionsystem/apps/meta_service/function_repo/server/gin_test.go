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

package server

import (
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"

	"meta_service/common/tls"
	"meta_service/function_repo/config"
)

var (
	repoCfg = &config.Configs{
		MutualTLSConfig: tls.MutualTLSConfig{
			TLSEnable:      true,
			RootCAFile:     "fake/path",
			ModuleKeyFile:  "fake/path/to/key",
			ModuleCertFile: "fake/path/to/cert",
			ServerName:     "test",
		},
	}
	e = engine{}
)

func Test_engine_Run(t *testing.T) {
	patches := gomonkey.NewPatches()
	defer patches.Reset()

	patches.ApplyGlobalVar(&config.RepoCfg, repoCfg)
	assert.NotNil(t, e.Run("test"))
}
