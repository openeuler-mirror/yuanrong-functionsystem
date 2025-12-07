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

// Package utils implements some utilities for common use.
package utils

import (
	"strconv"
	"strings"

	"meta_service/common/logger/log"
	"meta_service/common/urnutils"
	"meta_service/function_repo/config"
	"meta_service/function_repo/server"
)

const (
	urnSeparator    = ":"
	functionKeyword = "function"
	layerKeyword    = "layer"

	functionInfoPartsNum    = 6
	functionVerInfoPartsNum = 7
	layerVerInfoPartsNum    = 7
	aliasInfoPartsNum       = 7
	triggerInfoPartsNum     = 7
	businessIDIndex         = 2
	tenantInfoIndex         = 3
	functionNameIndex       = 5
	functionVersionIndex    = 6
	layerNameIndex          = 5
	layerVersionIndex       = 6
	aliasNameIndex          = 6
	verOrAliasIndex         = 6
)

func getTenantStr(tenantID, productID string) string {
	if productID == "" {
		return tenantID
	}
	return tenantID + urnutils.TenantProductSplitStr + productID
}

// BuildFunctionURN constructs function URN from tenant and function name.
// function URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:function:$functionName
func BuildFunctionURN(businessID, tenantID, productID, functionName string) string {
	prefix := config.RepoCfg.URNCfg.Prefix
	zone := config.RepoCfg.URNCfg.Zone

	parts := []string{prefix, zone, businessID, getTenantStr(tenantID, productID), functionKeyword, functionName}

	return strings.Join(parts, urnSeparator)
}

// BuildFunctionURNWithTenant constructs function URN from tenant and function name.
// function URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:function:$functionName
func BuildFunctionURNWithTenant(info server.TenantInfo, functionName string) string {
	return BuildFunctionURN(info.BusinessID, info.TenantID, info.ProductID, functionName)
}

// BuildFunctionVersionURN constructs function version URN from tenant, function name and version.
// function version URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:function:$functionName:$functionVersion
func BuildFunctionVersionURN(businessID, tenantID, productID, functionName, functionVersion string) string {
	prefix := config.RepoCfg.URNCfg.Prefix
	zone := config.RepoCfg.URNCfg.Zone

	parts := []string{prefix, zone, businessID, getTenantStr(tenantID, productID), functionKeyword, functionName,
		functionVersion}

	return strings.Join(parts, urnSeparator)
}

// BuildFunctionVersionURNWithTenant constructs function version URN from tenant, function name and version.
// function version URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:function:$functionName:$functionVersion
func BuildFunctionVersionURNWithTenant(info server.TenantInfo, functionName, functionVersion string) string {
	return BuildFunctionVersionURN(info.BusinessID, info.TenantID, info.ProductID, functionName, functionVersion)
}

// BuildLayerVersionURN constructs layer version URN from tenant, layer name and version.
// layer version URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:layer:$layerName:$versionNumber
func BuildLayerVersionURN(businessID, tenantID, productID, layerName, layerVersion string) string {
	prefix := config.RepoCfg.URNCfg.Prefix
	zone := config.RepoCfg.URNCfg.Zone

	parts := []string{prefix, zone, businessID, getTenantStr(tenantID, productID), layerKeyword, layerName,
		layerVersion}
	return strings.Join(parts, urnSeparator)
}

// BuildLayerURN constructs layer version URN from tenant, layer name.
// layer version URN format:
// $urnPrefix:$urnZone:$businessId:$tenantId@$productId:layer:$layerName
func BuildLayerURN(businessID, tenantID, productID, layerName string) string {
	prefix := config.RepoCfg.URNCfg.Prefix
	zone := config.RepoCfg.URNCfg.Zone

	parts := []string{prefix, zone, businessID, getTenantStr(tenantID, productID), layerKeyword, layerName}

	return strings.Join(parts, urnSeparator)
}

// BuildAliasURN constructs alias URN from tenant, function name and alias name.
// alias URN format:
// $function_urn:alias_name
func BuildAliasURN(businessID, tenantID, productID, functionName, aliasName string) string {
	parts := []string{BuildFunctionURN(businessID, tenantID, productID, functionName), aliasName}
	return strings.Join(parts, urnSeparator)
}

// BuildTriggerURN constructs trigger URN from tenant, function name and function version or alias name.
// trigger URN format:
// $function_urn:version or aliasname
func BuildTriggerURN(info server.TenantInfo, functionName, verOrAlias string) string {
	parts := []string{BuildFunctionURN(info.BusinessID, info.TenantID, info.ProductID, functionName), verOrAlias}
	return strings.Join(parts, urnSeparator)
}

// FunctionInfo records tenant and function name of a function URN.
type FunctionInfo struct {
	BusinessID   string
	TenantInfo   string
	FunctionName string
}

// ParseFunctionInfoFromURN returns FunctionInfo by a function URN.
func ParseFunctionInfoFromURN(s string) (FunctionInfo, bool) {
	infos := strings.Split(s, urnSeparator)
	if len(infos) != functionInfoPartsNum {
		return FunctionInfo{}, false
	}
	return FunctionInfo{
		BusinessID:   infos[businessIDIndex],
		TenantInfo:   infos[tenantInfoIndex],
		FunctionName: infos[functionNameIndex]}, true
}

// FunctionVersionInfo records tenant and function name and version of a function version URN.
type FunctionVersionInfo struct {
	FunctionInfo
	FunctionVersion string
}

// ParseFunctionVerInfoFromURN returns FunctionVersionInfo by a function version URN.
func ParseFunctionVerInfoFromURN(s string) (FunctionVersionInfo, bool) {
	infos := strings.Split(s, urnSeparator)
	if len(infos) != functionVerInfoPartsNum {
		return FunctionVersionInfo{}, false
	}
	return FunctionVersionInfo{
		FunctionInfo: FunctionInfo{
			infos[businessIDIndex],
			infos[tenantInfoIndex],
			infos[functionNameIndex]},
		FunctionVersion: infos[functionVersionIndex]}, true
}

// LayerVersionInfo records tenant and layer name and version info of a layer version URN.
type LayerVersionInfo struct {
	BusinessID   string
	TenantInfo   string
	LayerName    string
	LayerVersion int
}

// ParseLayerVerInfoFromURN returns LayerVersionInfo by a layer version URN.
func ParseLayerVerInfoFromURN(s string) (LayerVersionInfo, bool) {
	infos := strings.Split(s, urnSeparator)
	if len(infos) != layerVerInfoPartsNum {
		return LayerVersionInfo{}, false
	}
	version, err := strconv.Atoi(infos[layerVersionIndex])
	if err != nil {
		log.GetLogger().Errorf("cannot parse layer version: %s", err.Error())
		return LayerVersionInfo{}, false
	}
	return LayerVersionInfo{infos[businessIDIndex],
		infos[tenantInfoIndex],
		infos[layerNameIndex],
		version}, true
}

// AliasInfo records tenant and function name and alias name of a alias URN.
type AliasInfo struct {
	FunctionInfo
	AliasName string
}

// ParseAliasInfoFromURN returns AliasInfo by a alias URN.
func ParseAliasInfoFromURN(s string) (AliasInfo, bool) {
	infos := strings.Split(s, urnSeparator)
	if len(infos) != aliasInfoPartsNum {
		return AliasInfo{}, false
	}
	return AliasInfo{
		FunctionInfo: FunctionInfo{
			infos[businessIDIndex],
			infos[tenantInfoIndex],
			infos[functionNameIndex]},
		AliasName: infos[aliasNameIndex]}, true
}

// TriggerInfo records tenant and function name and function versionor or alias name.
type TriggerInfo struct {
	FunctionInfo
	VerOrAlias string
}

// ParseTriggerInfoFromURN returns TriggerInfo by a trigger URN.
func ParseTriggerInfoFromURN(s string) (TriggerInfo, bool) {
	infos := strings.Split(s, urnSeparator)
	if len(infos) == triggerInfoPartsNum {
		return TriggerInfo{
			FunctionInfo{
				infos[businessIDIndex],
				infos[tenantInfoIndex],
				infos[functionNameIndex]},
			infos[verOrAliasIndex]}, true
	}
	if len(infos) == functionInfoPartsNum {
		return TriggerInfo{
			FunctionInfo{
				infos[businessIDIndex],
				infos[tenantInfoIndex],
				infos[functionNameIndex]},
			""}, true
	}
	return TriggerInfo{}, false
}
