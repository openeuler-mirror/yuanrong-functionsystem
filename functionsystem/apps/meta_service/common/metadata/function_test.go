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

// Package metadata define struct of metadata stored in storage like etcd
package metadata

import "testing"

func TestFunctionKey_FullName(t *testing.T) {
	funcKey := &FunctionKey{
		ServiceID: "serviceA",
		FuncName:  "helloWorld",
	}

	res := funcKey.FullName()
	if res != "0-serviceA-helloWorld" {
		t.Errorf("failed to get full name")
	}

}
func TestFunctionKey_ToAnonymousString(t *testing.T) {
	funcKey := &FunctionKey{
		BusinessID:  "1",
		TenantID:    "1234",
		ServiceID:   "serviceA",
		FuncName:    "helloWorld",
		FuncVersion: "$latest",
	}
	res := funcKey.ToAnonymousString()
	if res != "****-1-serviceA-helloWorld-$latest" {
		t.Errorf("failed to AnonymousString")
	}
}
