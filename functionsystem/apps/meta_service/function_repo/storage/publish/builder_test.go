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

package publish

import (
	"testing"

	"meta_service/function_repo/server"

	"github.com/stretchr/testify/assert"
)

func genTenantInfo(productID string) server.TenantInfo {
	return server.TenantInfo{
		BusinessID: "a",
		TenantID:   "a",
		ProductID:  productID,
	}
}

func TestBuildFunctionRegisterKey(t *testing.T) {
	info := genTenantInfo("")
	key := BuildFunctionRegisterKey(info, "a", "", "faas")
	assert.Equal(t, "/sn/functions/business/a/tenant/a/function/a/version/", key)
	key = BuildFunctionRegisterKey(info, "a", "", "yrlib")
	assert.Equal(t, "/yr/functions/business/a/tenant/a/function/a/version/", key)
}

func TestBuildFunctionRegisterKeyWithProductID(t *testing.T) {
	info := genTenantInfo("a")
	key := BuildFunctionRegisterKey(info, "a", "", "faas")
	assert.Equal(t, "/sn/functions/business/a/tenant/a@a/function/a/version/", key)
}

func TestBuildFunctionRegisterKeyWithVersion(t *testing.T) {
	info := genTenantInfo("")
	key := BuildFunctionRegisterKey(info, "a", "a", "faas")
	assert.Equal(t, "/sn/functions/business/a/tenant/a/function/a/version/a", key)
}

func TestBuildFunctionRegisterKeyWithoutName(t *testing.T) {
	info := genTenantInfo("")
	key := BuildFunctionRegisterKey(info, "", "a", "faas")
	assert.Equal(t, "/sn/functions/business/a/tenant/a/function/", key)
}

func TestBuildTriggerRegisterKey(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "a", "a", "a", "a")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a/function/a/version/a/a", key)
}

func TestBuildTriggerRegisterKeyWithProductID(t *testing.T) {
	info := genTenantInfo("a")
	key := BuildTriggerRegisterKey(info, "a", "a", "a", "a")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a@a/function/a/version/a/a", key)
}

func TestBuildTriggerRegisterKeyWithoutVersion(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "a", "a", "", "")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a/function/a/version/", key)
}

func TestBuildTriggerRegisterKeyWithoutTriggerID(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "a", "a", "a", "")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a/function/a/version/a/", key)
}

func TestBuildTriggerRegisterKeyWithoutVersionWithTriggerID(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "a", "a", "", "a")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a/function/a/version/", key)
}

func TestBuildTriggerRegisterKeyWithoutTriggerType(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "a", "", "a", "a")
	assert.Equal(t, "/sn/triggers/triggerType/", key)
}

func TestBuildTriggerRegisterKeyWithoutName(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTriggerRegisterKey(info, "", "a", "a", "a")
	assert.Equal(t, "/sn/triggers/triggerType/a/business/a/tenant/a/function/", key)
}

func TestBuildAliasRegisterKey(t *testing.T) {
	info := genTenantInfo("")
	key := BuildAliasRegisterKey(info, "a", "a")
	assert.Equal(t, "/sn/aliases/business/a/tenant/a/function/a/a", key)
}

func TestBuildAliasRegisterKeyWithoutAliasName(t *testing.T) {
	info := genTenantInfo("")
	key := BuildAliasRegisterKey(info, "a", "")
	assert.Equal(t, "/sn/aliases/business/a/tenant/a/function/a/", key)
}

func TestBuildAliasRegisterKeyWithoutName(t *testing.T) {
	info := genTenantInfo("")
	key := BuildAliasRegisterKey(info, "", "a")
	assert.Equal(t, "/sn/aliases/business/a/tenant/a/function/", key)
}

func TestBuildAliasRegisterKeyWithProductID(t *testing.T) {
	info := genTenantInfo("a")
	key := BuildAliasRegisterKey(info, "a", "a")
	assert.Equal(t, "/sn/aliases/business/a/tenant/a@a/function/a/a", key)
}

func TestBuildTraceChainRegisterKey(t *testing.T) {
	info := genTenantInfo("")
	key := BuildTraceChainRegisterKey("a", info)
	assert.Equal(t, "/sn/functionchains/business/a/tenant/a/function/a", key)
}

func TestBuildTraceChainRegisterKeyWithProductID(t *testing.T) {
	info := genTenantInfo("a")
	key := BuildTraceChainRegisterKey("a", info)
	assert.Equal(t, "/sn/functionchains/business/a/tenant/a@a/function/a", key)
}

func TestBuildTraceChainRegisterKeyWithoutName(t *testing.T) {
	info := genTenantInfo("a")
	key := BuildTraceChainRegisterKey("", info)
	assert.Equal(t, "/sn/functionchains/business/a/tenant/a@a/function/", key)
}
