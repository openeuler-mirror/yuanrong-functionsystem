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
	"strings"

	"meta_service/function_repo/server"
	"meta_service/function_repo/utils/constants"

	"meta_service/common/urnutils"
)

func appendTenantInfo(keys []string, info server.TenantInfo) []string {
	keys = append(keys, constants.BusinessKey, info.BusinessID, constants.TenantKey)
	if info.ProductID != "" {
		keys = append(keys, info.TenantID+urnutils.TenantProductSplitStr+info.ProductID)
	} else {
		keys = append(keys, info.TenantID)
	}
	return keys
}

// BuildFunctionRegisterKey returns the etcd key of the registered function information.
func BuildFunctionRegisterKey(info server.TenantInfo, name, funcVersion, kind string) string {
	// format(faas): /sn/functions/business/<businessID>/tenant/<tenantID>/function/<functionName>/version/<version>
	// format(yrlib): /yr/functions/business/<businessID>/tenant/<tenantID>/function/<functionName>/version/<version>
	keys := []string{constants.YRFunctionPrefix}
	if kind == constants.Faas {
		keys = []string{constants.FunctionPrefix}
	}
	keys = appendTenantInfo(keys, info)
	keys = append(keys, constants.ResourceKey, name)
	if name == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = append(keys, constants.VersionKey, funcVersion)
	return strings.Join(keys, constants.ETCDKeySeparator)
}

// BuildInstanceRegisterKey return the etcd key of the registered function instance meta information.
func BuildInstanceRegisterKey(info server.TenantInfo, name, funcVersion, clusterID string) string {
	// format: /instances/business/yrk/cluster/cluster001/tenant/<tenantID>/function/<functionName>/version/<version>
	keys := []string{
		constants.InstancePrefix, constants.BusinessKey, "yrk", constants.ClusterKey, clusterID,
		constants.TenantKey,
	}
	keys = append(keys, info.TenantID, constants.ResourceKey, name)
	if name == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = append(keys, constants.VersionKey, funcVersion)
	return strings.Join(keys, constants.ETCDKeySeparator)
}

// BuildTriggerRegisterKey return the etcd key of the registered Trigger information.
func BuildTriggerRegisterKey(
	info server.TenantInfo, funcName, triggerType, verOrAlias, triggerID string,
) string {
	// format: /sn/triggers/triggerType/<triggerType>/business/<businessID>/tenant/<tenantID>\
	// /function/<functionName>/version/<version>/<TriggerID>
	keys := []string{constants.TriggerPrefix, constants.TriggerTypeKey, triggerType}
	if triggerType == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = appendTenantInfo(keys, info)
	keys = append(keys, constants.ResourceKey, funcName)
	if funcName == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = append(keys, constants.VersionKey, verOrAlias)
	if verOrAlias == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = append(keys, triggerID)
	return strings.Join(keys, constants.ETCDKeySeparator)
}

// BuildAliasRegisterKey return the etcd key of the registered Alias information.
func BuildAliasRegisterKey(info server.TenantInfo, funcName, aliasName string) string {
	// format: /sn/aliases/business/<Business>/Tenant/<TenantID>/function/<funcName>/<aliasName>
	keys := []string{constants.AliasPrefix}
	keys = appendTenantInfo(keys, info)
	keys = append(keys, constants.ResourceKey, funcName)
	if funcName == "" {
		return strings.Join(keys, constants.ETCDKeySeparator)
	}
	keys = append(keys, aliasName)
	return strings.Join(keys, constants.ETCDKeySeparator)
}

// BuildTraceChainRegisterKey return the etcd key of the registered TraceChain information.
func BuildTraceChainRegisterKey(name string, info server.TenantInfo) string {
	// format: /sn/functionchains/business/<Business>/Tenant/<TenantID>/function/<funcName>
	keys := []string{constants.ChainPrefix}
	keys = appendTenantInfo(keys, info)
	keys = append(keys, constants.ResourceKey, name)
	return strings.Join(keys, constants.ETCDKeySeparator)
}
