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

package model

import (
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/config"
)

func TestCheckParamsFailed(t *testing.T) {
	if err := config.InitConfig("/home/sn/repo/config.json"); err != nil {
		t.Fatalf(err.Error())
	}
	convey.Convey("TestCheckRuntimeFailed", t, func() {
		c := config.Configs{}
		r := &FunctionCreateRequest{
			FunctionBasicInfo: FunctionBasicInfo{
				Layers: []string{"HOME", "USERPROFILE"},
				CPU:    1,
				Memory: 2,
			},
		}
		convey.Convey("exceed layer", func() {
			c.FunctionCfg.LayerMax = 0
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("memory not in config", func() {
			c.FunctionCfg.LayerMax = 10
			c.FunctionCfg.DefaultCfg.CPUList = []int64{1}
			c.FunctionCfg.DefaultCfg.MemoryList = []int64{1}
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("CPU and memory do not match", func() {
			c.FunctionCfg.LayerMax = 10
			c.FunctionCfg.DefaultCfg.CPUList = []int64{1}
			c.FunctionCfg.DefaultCfg.MemoryList = []int64{1, 2}
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("UpdateRequestFailed", func() {
			ur := &FunctionUpdateRequest{}
			err := ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
	})
}

func TestCheckRuntimeFailed(t *testing.T) {
	convey.Convey("TestCheckRuntimeFailed", t, func() {
		patches := gomonkey.NewPatches()
		defer patches.Reset()
		patches.ApplyFunc(checkLayersNum, func(layerLen int, configs config.Configs) error {
			return nil
		})
		patches.ApplyFunc(checkCPUAndMemory, func(cpu, memory int64) error {
			return nil
		})
		c := config.Configs{}
		convey.Convey(" runtime is not valid", func() {
			r := &FunctionCreateRequest{}
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("UpdateRequestFailed", func() {
			config.RepoCfg = new(config.Configs)
			config.RepoCfg.RuntimeType = []string{"test"}
			ur := &FunctionUpdateRequest{}
			err := ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
	})
}

func TestValidateFailed(t *testing.T) {
	convey.Convey("TestValidateFailed", t, func() {
		patches := gomonkey.NewPatches()
		defer patches.Reset()
		patches.ApplyFunc(checkLayersNum, func(layerLen int, configs config.Configs) error {
			return nil
		})
		patches.ApplyFunc(checkCPUAndMemory, func(cpu, memory int64) error {
			return nil
		})
		patches.ApplyFunc(checkRuntime, func(configs config.Configs, runtime string) error { return nil })
		c := config.Configs{}
		r := &FunctionCreateRequest{}
		ur := &FunctionUpdateRequest{}
		config.RepoCfg = new(config.Configs)
		config.RepoCfg.RuntimeType = []string{"test"}
		c.FunctionCfg.DefaultCfg.Timeout = 0
		convey.Convey("minInstance must be an integer", func() {
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
			err = ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("maxInstance must be an integer", func() {
			c.FunctionCfg.DefaultCfg.DefaultMinInstance = "1"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
			err = ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("concurrentNum must be an integer", func() {
			c.FunctionCfg.DefaultCfg.DefaultMinInstance = "1"
			c.FunctionCfg.DefaultCfg.DefaultMaxInstance = "1"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
			err = ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("TestWorker Instance Failed", func() {
			c.FunctionCfg.DefaultCfg.DefaultMinInstance = "1"
			c.FunctionCfg.DefaultCfg.DefaultMaxInstance = "1"
			c.FunctionCfg.DefaultCfg.DefaultConcurrentNum = "1"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
			err = ur.Validate(c)
			convey.ShouldNotBeNil(err)
		})
	})
}

func TestCheckWorkerParams(t *testing.T) {
	convey.Convey("TestCheckWorkerParamsFailed", t, func() {
		patches := gomonkey.NewPatches()
		defer patches.Reset()
		patches.ApplyFunc(checkLayersNum, func(layerLen int, configs config.Configs) error {
			return nil
		})
		patches.ApplyFunc(checkCPUAndMemory, func(cpu, memory int64) error {
			return nil
		})
		patches.ApplyFunc(checkRuntime, func(configs config.Configs, runtime string) error { return nil })
		c := config.Configs{}
		r := &FunctionCreateRequest{
			FunctionBasicInfo: FunctionBasicInfo{
				MaxInstance:   "1",
				MinInstance:   "1",
				ConcurrentNum: "1",
			},
		}
		convey.Convey("minInstance must be smaller than maxInstance", func() {
			r.MaxInstance = "0"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("minInstance should be bigger 0", func() {
			r.MinInstance = "-1"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("maxInstanceUpperLimit must be an integer", func() {
			c.FunctionCfg.DefaultCfg.MaxInstanceUpperLimit = "a"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("maxInstance must be smaller than workerInstanceMax", func() {
			c.FunctionCfg.DefaultCfg.MaxInstanceUpperLimit = "0"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("concurrentNum must be an integer ", func() {
			c.FunctionCfg.DefaultCfg.MaxInstanceUpperLimit = "2"
			c.FunctionCfg.DefaultCfg.ConcurrentNumUpperLimit = "a"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("concurrentNum must be smaller than workerConcurrentNumMax", func() {
			c.FunctionCfg.DefaultCfg.MaxInstanceUpperLimit = "2"
			c.FunctionCfg.DefaultCfg.ConcurrentNumUpperLimit = "0"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
		convey.Convey("concurrentNum should be bigger than 1", func() {
			c.FunctionCfg.DefaultCfg.MaxInstanceUpperLimit = "2"
			c.FunctionCfg.DefaultCfg.ConcurrentNumUpperLimit = "2"
			r.ConcurrentNum = "0"
			err := r.Validate(c)
			convey.ShouldNotBeNil(err)
		})
	})
}
