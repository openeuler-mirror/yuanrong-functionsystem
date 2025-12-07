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

// Package test prepare for Unit Tests
package test

import (
	"fmt"
	"meta_servicemeta_service/test/fakecontext"

	"meta_service/function_repo/storage"
	"meta_service/function_repo/utils"
)

const (
	// ConfigPath -
	ConfigPath = "/home/sn/repo/config.json"
)

// ResetETCD delete data in /repo from ETCD
func ResetETCD() {
	// delete etcd value
	context := fakecontext.NewContext()
	txn := storage.NewTxn(context)
	defer txn.Cancel()
	txn.DeleteRange(utils.MetadataPrefix())
	err := txn.Commit()
	if err != nil {
		fmt.Printf("failed to commit %s", err)
	}
}
