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
	"testing"

	"github.com/stretchr/testify/assert"

	"meta_service/function_repo/config"
	"meta_service/function_repo/server"
)

func TestBuildFunctionURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	urn := BuildFunctionURN("b", "t", "p", "fn")
	assert.Equal(t, "p:z:b:t@p:function:fn", urn)

	urn = BuildFunctionURN("b", "t", "", "fn")
	assert.Equal(t, "p:z:b:t:function:fn", urn)
}

func TestBuildFunctionVersionURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	urn := BuildFunctionVersionURN("b", "t", "p", "fn", "fv")
	assert.Equal(t, "p:z:b:t@p:function:fn:fv", urn)

	urn = BuildFunctionVersionURN("b", "t", "", "fn", "fv")
	assert.Equal(t, "p:z:b:t:function:fn:fv", urn)
}

func TestBuildLayerVersionURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	urn := BuildLayerVersionURN("b", "t", "p", "ln", "lv")
	assert.Equal(t, "p:z:b:t@p:layer:ln:lv", urn)

	urn = BuildLayerVersionURN("b", "t", "", "ln", "lv")
	assert.Equal(t, "p:z:b:t:layer:ln:lv", urn)
}

func TestBuildLayerUrn(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	urn := BuildLayerURN("b", "t", "p", "ln")
	assert.Equal(t, "p:z:b:t@p:layer:ln", urn)

	urn = BuildLayerURN("b", "t", "", "ln")
	assert.Equal(t, "p:z:b:t:layer:ln", urn)
}

func TestBuildFunctionUrnWithTenant(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	{
		info := server.TenantInfo{"b", "t", "p"}
		urn := BuildFunctionURNWithTenant(info, "fn")
		assert.Equal(t, "p:z:b:t@p:function:fn", urn)
	}
	{
		info := server.TenantInfo{"b", "t", ""}
		urn := BuildFunctionURNWithTenant(info, "fn")
		assert.Equal(t, "p:z:b:t:function:fn", urn)
	}
}

func TestBuildFunctionVersionUrnWithTenant(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	{
		info := server.TenantInfo{"b", "t", "p"}
		urn := BuildFunctionVersionURNWithTenant(info, "fn", "fv")
		assert.Equal(t, "p:z:b:t@p:function:fn:fv", urn)
	}
	{
		info := server.TenantInfo{"b", "t", ""}
		urn := BuildFunctionVersionURNWithTenant(info, "fn", "fv")
		assert.Equal(t, "p:z:b:t:function:fn:fv", urn)
	}
}

func TestBuildTriggerUrn(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	{
		info := server.TenantInfo{"b", "t", "p"}
		urn := BuildTriggerURN(info, "fn", "va")
		assert.Equal(t, "p:z:b:t@p:function:fn:va", urn)
	}
	{
		info := server.TenantInfo{"b", "t", ""}
		urn := BuildTriggerURN(info, "fn", "va")
		assert.Equal(t, "p:z:b:t:function:fn:va", urn)
	}
}

func TestBuildAliasURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	urn := BuildAliasURN("b", "t", "p", "fn", "an")
	assert.Equal(t, "p:z:b:t@p:function:fn:an", urn)
	urn = BuildAliasURN("b", "t", "", "fn", "an")
	assert.Equal(t, "p:z:b:t:function:fn:an", urn)
}

func TestParseFunctionInfoFromURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	info, ok := ParseFunctionInfoFromURN("p:z:b:t@p:function:fn")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t@p", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)

	info, ok = ParseFunctionInfoFromURN("p:z:b:t:function:fn")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)

	info, ok = ParseFunctionInfoFromURN("p:z:b:t")
	assert.Equal(t, false, ok)

}

func TestParseFunctionVerInfoFromURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	info, ok := ParseFunctionVerInfoFromURN("p:z:b:t@p:function:fn:fv")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t@p", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "fv", info.FunctionVersion)

	info, ok = ParseFunctionVerInfoFromURN("p:z:b:t:function:fn:fv")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "fv", info.FunctionVersion)

	info, ok = ParseFunctionVerInfoFromURN("p:z:b:t")
	assert.Equal(t, false, ok)
}

func TestParseLayerVerInfoFromURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	info, ok := ParseLayerVerInfoFromURN("p:z:b:t@p:layer:ln:1")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t@p", info.TenantInfo)
	assert.Equal(t, "ln", info.LayerName)
	assert.Equal(t, 1, info.LayerVersion)

	info, ok = ParseLayerVerInfoFromURN("p:z:b:t@p:layer:ln:a")
	assert.Equal(t, false, ok)

	info, ok = ParseLayerVerInfoFromURN("p:z:b:t:layer:ln:1")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "ln", info.LayerName)
	assert.Equal(t, 1, info.LayerVersion)

	info, ok = ParseLayerVerInfoFromURN("p:z:b:t")
	assert.Equal(t, false, ok)
}

func TestParseAliasInfoFromURN(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	info, ok := ParseAliasInfoFromURN("p:z:b:t@p:function:fn:an")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t@p", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "an", info.AliasName)

	info, ok = ParseAliasInfoFromURN("p:z:b:t:function:fn:an")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "an", info.AliasName)

	info, ok = ParseAliasInfoFromURN("p:z:b:t")
	assert.Equal(t, false, ok)
}

func TestParseTriggerInfoFromUrn(t *testing.T) {
	config.RepoCfg = new(config.Configs)
	config.RepoCfg.URNCfg.Prefix = "p"
	config.RepoCfg.URNCfg.Zone = "z"

	info, ok := ParseTriggerInfoFromURN("p:z:b:t@p:function:fn:va")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t@p", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "va", info.VerOrAlias)

	info, ok = ParseTriggerInfoFromURN("p:z:b:t:function:fn:va")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "va", info.VerOrAlias)

	info, ok = ParseTriggerInfoFromURN("p:z:b:t:function:fn")
	assert.Equal(t, true, ok)
	assert.Equal(t, "b", info.BusinessID)
	assert.Equal(t, "t", info.TenantInfo)
	assert.Equal(t, "fn", info.FunctionName)
	assert.Equal(t, "", info.VerOrAlias)

	info, ok = ParseTriggerInfoFromURN("p:z:b:t:function")
	assert.Equal(t, false, ok)
}
