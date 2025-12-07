/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

package etcd

import (
	"errors"
	"testing"

	"meta_service/function_repo/config"

	"meta_service/common/etcd3"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"
)

func TestInitializeFailed(t *testing.T) {
	convey.Convey("TestNewClientFailed", t, func() {
		patches := gomonkey.NewPatches()
		defer patches.Reset()
		patches.ApplyFunc(etcd3.NewEtcdWatcher, func(config etcd3.EtcdConfig) (*etcd3.EtcdClient, error) {
			return &etcd3.EtcdClient{}, errors.New("test error")
		})
		config.RepoCfg = new(config.Configs)
		_, err := NewClient()
		convey.So(err, convey.ShouldNotEqual, nil)
	})
}
