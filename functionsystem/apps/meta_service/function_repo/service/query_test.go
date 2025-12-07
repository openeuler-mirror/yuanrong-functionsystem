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

package service

import (
	"testing"

	"meta_service/function_repo/server"
	"meta_service/function_repo/test/fakecontext"
)

func TestCheckContextInfo(t *testing.T) {
	ctx := fakecontext.NewContext()
	info := server.TenantInfo{
		BusinessID: "businessid",
		TenantID:   "tenantid",
		ProductID:  "productid",
	}
	ctx.InitTenantInfo(info)

	funcID := "sn:cn:businessid:tenantid@productid:function:test:$latest"
	err := checkContextInfo(ctx, funcID)
	if err != nil {
		t.Errorf("tenantInfo is empty")
	}

	funcID = "sn:cn:businessid:tenantid@productid111:function:test:$latest"
	err = checkContextInfo(ctx, funcID)
	if err == nil {
		t.Errorf("tenantInfo is empty")
	}

	funcID = "sn:cn:businessid:tenantid@:function:test:$latest"
	err = checkContextInfo(ctx, funcID)
	if err == nil {
		t.Errorf("tenantInfo is empty")
	}

	funcID = "sn:cn:businessid:tenantid:function:test:$latest"
	err = checkContextInfo(ctx, funcID)
	if err == nil {
		t.Errorf("tenantInfo is empty")
	}
}

func TestCheckAndGetVerOrAlias(t *testing.T) {
	funcID := "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test:$latest"
	qualifier := "123"
	info, err := CheckAndGetVerOrAlias(funcID, qualifier)
	if err == nil {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test:$latest"
	qualifier = ""
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "$latest" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test:12354515"
	qualifier = ""
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "12354515" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test:12354515ZSDSSas"
	qualifier = ""
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	// yes, now 12354515ZSDSSas can be an version (start with digits, not meet alias requirement)
	if err != nil {
		t.Errorf("funcID is error, funcID is %s", funcID)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test:myalias"
	qualifier = ""
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "" && info.AliasName == "myalias") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test"
	qualifier = "myalias12312"
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "" && info.AliasName == "myalias12312") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test"
	qualifier = "myalias1233ZZZADS"
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	// yes, now myalias1233ZZZADS can be an version (len 17, longer than alias requirement)
	if err != nil {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test"
	qualifier = "myalias1233ZZZADS000000000000000000000aaa00"
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if err == nil {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test"
	qualifier = "123545"
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "123545" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcID = "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:test"
	qualifier = ""
	info, err = CheckAndGetVerOrAlias(funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}
}

func TestGetFunctionQueryInfoByFuncID(t *testing.T) {
	ctx := fakecontext.NewContext()
	info := server.TenantInfo{
		BusinessID: "businessid",
		TenantID:   "tenantid",
		ProductID:  "productid",
	}
	ctx.InitTenantInfo(info)
	funcID := "sn:cn:businessid:tenantid@productid:function:test:$latest"
	qualifier := ""
	funcInfo, err := parseFunctionInfoByFuncID(ctx, funcID, qualifier)
	if !(err == nil && funcInfo.FunctionName == "test" && funcInfo.FunctionVersion == "$latest" && funcInfo.AliasName == "") {
		t.Errorf("tenantInfo is err")
	}

	funcID = "sn:cn:businessid:tenantid@productid111:function:test:$latest"
	qualifier = ""
	funcInfo, err = parseFunctionInfoByFuncID(ctx, funcID, qualifier)
	if err == nil {
		t.Errorf("tenantInfo is empty")
	}

	funcID = "sn:cn:businessid:tenantid@productid:function:test:12354515"
	qualifier = ""
	funcInfo, err = parseFunctionInfoByFuncID(ctx, funcID, qualifier)
	if !(err == nil && funcInfo.FunctionName == "test" && funcInfo.FunctionVersion == "12354515" && funcInfo.AliasName == "") {
		t.Errorf("funcID is err")
	}

	funcID = "sn:cn:businessid:tenantid@productid:function:test:serses21323"
	qualifier = ""
	funcInfo, err = parseFunctionInfoByFuncID(ctx, funcID, qualifier)
	if !(err == nil && funcInfo.FunctionName == "test" && funcInfo.FunctionVersion == "" && funcInfo.AliasName == "serses21323") {
		t.Errorf("funcID is err")
	}

	funcID = "sn:cn:businessid:tenantid@productid:function:test:serses21323"
	qualifier = "123"
	funcInfo, err = parseFunctionInfoByFuncID(ctx, funcID, qualifier)
	if err == nil {
		t.Errorf("qualifier is empty")
	}
}

func TestGetFunctionQueryInfoByName(t *testing.T) {
	funcName := "0-cpptest-helloworld"
	qualifier := "$latest"
	info, err := parseFunctionInfoByName(funcName, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptest-helloworld" && info.FunctionVersion == "$latest" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcName = "0-cpptest-helloworld"
	qualifier = "1223323"
	info, err = parseFunctionInfoByName(funcName, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptest-helloworld" && info.FunctionVersion == "1223323" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcName = "0-cpptest-helloworld"
	qualifier = "sdsddwae112"
	info, err = parseFunctionInfoByName(funcName, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptest-helloworld" && info.FunctionVersion == "" && info.AliasName == "sdsddwae112") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcName = "0-cpptest-helloworld"
	qualifier = ""
	info, err = parseFunctionInfoByName(funcName, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptest-helloworld" && info.FunctionVersion == "" && info.AliasName == "") {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}

	funcName = "0-cpptest-helloworld"
	qualifier = "ZSDW123xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
	info, err = parseFunctionInfoByName(funcName, qualifier)
	if err == nil {
		t.Errorf("qualifier is error, qualifier is %s", qualifier)
	}
}

func TestGetFunctionQueryInfo(t *testing.T) {
	ctx := fakecontext.NewContext()
	tenInfo := server.TenantInfo{
		BusinessID: "businessid",
		TenantID:   "tenantid",
		ProductID:  "productid",
	}
	ctx.InitTenantInfo(tenInfo)

	funcID := "sn:cn:businessid:tenantid@productid:function:test:$latest"
	qualifier := ""
	info, err := ParseFunctionInfo(ctx, funcID, qualifier)
	if !(err == nil && info.FunctionName == "test" && info.FunctionVersion == "$latest" && info.AliasName == "") {
		t.Errorf("tenantInfo is err")
	}

	funcID = "sn:cn:businessid:tenantid@productid11:function:test:$latest"
	qualifier = ""
	info, err = ParseFunctionInfo(ctx, funcID, qualifier)
	if err == nil {
		t.Errorf("tenantInfo is err")
	}

	funcID = "0-cpptest-helloworld"
	qualifier = "123"
	info, err = ParseFunctionInfo(ctx, funcID, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptest-helloworld" && info.FunctionVersion == "123" && info.AliasName == "") {
		t.Errorf("tenantInfo is err")
	}

	funcID = "0-cpptestXX-hew001-001"
	qualifier = "123"
	info, err = ParseFunctionInfo(ctx, funcID, qualifier)
	if !(err == nil && info.FunctionName == "0-cpptestXX-hew001-001" && info.FunctionVersion == "123" && info.AliasName == "") {
		t.Errorf("tenantInfo is err")
	}

	funcID = "cpptest-helloworld"
	qualifier = ""
	info, err = ParseFunctionInfo(ctx, funcID, qualifier)
	if !(err == nil && info.FunctionName == "cpptest-helloworld" && info.FunctionVersion == "" && info.AliasName == "") {
		t.Errorf("tenantInfo is err")
	}
}
