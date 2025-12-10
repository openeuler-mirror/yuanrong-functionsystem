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

// Package etcd creates an etcdv3 client for meta service.
package etcd

import (
	"meta_service/common/logger/log"
	"meta_service/function_repo/config"

	"meta_service/common/etcd3"
)

// NewClient returns a etcd client
func NewClient() (*etcd3.EtcdClient, error) {
	cli, err := etcd3.NewEtcdWatcher(config.RepoCfg.EtcdCfg)
	if err != nil {
		log.GetLogger().Errorf("failed to new etcd client :%s", err)
		return nil, err
	}
	return cli, nil
}

// NewMetaClient returns a meta etcd client
func NewMetaClient() (*etcd3.EtcdClient, error) {
	cli, err := etcd3.NewEtcdWatcher(config.RepoCfg.MetaEtcdCfg)
	if err != nil {
		log.GetLogger().Errorf("failed to new meta etcd client :%s", err)
		return nil, err
	}
	return cli, nil
}
