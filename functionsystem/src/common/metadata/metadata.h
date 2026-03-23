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
#ifndef METADATA_H
#define METADATA_H

#include <google/protobuf/util/json_util.h>
#include <unordered_map>

#include "metadata_type.h"
#include "common/utils/files.h"

namespace functionsystem {
using namespace functionsystem::resource_view;

inline bool ContainStr(const std::string &str, const std::string &subStr)
{
    return str.find(subStr) != std::string::npos;
}

inline std::string GetFunctionHashTag(const FunctionMeta &funcMeta)
{
    return std::to_string(std::hash<std::string>{}(funcMeta.funcMetaData.name + funcMeta.funcMetaData.revisionId));
}

/**
 * trans to protobuf struct of InstanceInfo from json string
 * @param instanceInfo: protobuf struct of InstanceInfo
 * @param jsonStr: json string
 * @return true if trans success, false if trans failed
 */
bool TransToInstanceInfoFromJson(InstanceInfo &instanceInfo, const std::string &jsonStr);

bool TransToDebugInstanceInfoFromJson(messages::DebugInstanceInfo &debugInstInfo, const std::string &jsonStr);

/**
 * trans to protobuf struct of GroupInfo from json string
 * @param groupInfo: protobuf struct of GroupInfo
 * @param jsonStr: json string
 * @return true if trans success, false if trans failed
 */
bool TransToGroupInfoFromJson(messages::GroupInfo &groupInfo, const std::string &jsonStr);

/**
 * trans to json string from protobuf struct of InstanceInfo
 * @param jsonStr: json string
 * @param instanceInfo: protobuf struct of InstanceInfo
 * @return true if trans success, false if trans failed
 */
bool TransToJsonFromInstanceInfo(std::string &jsonStr, const InstanceInfo &instanceInfo);

/**
 * trans to json string from protobuf struct of GroupInfo
 * @param jsonStr: json string
 * @param groupInfo: protobuf struct of GroupInfo
 * @return true if trans success, false if trans failed
 */
bool TransToJsonFromGroupInfo(std::string &jsonStr, const messages::GroupInfo &groupInfo);

/**
 * trans to protobuf struct of RouteInfo from json string
 * @param routeInfo: protobuf struct of RouteInfo
 * @param jsonStr: json string
 * @return true if trans success, false if trans failed
 */
bool TransToRouteInfoFromJson(resources::RouteInfo &routeInfo, const std::string &jsonStr);

/**
 * trans to json string from protobuf struct of RouteInfo
 * @param jsonStr: json string
 * @param routeInfo: protobuf struct of RouteInfo
 * @return true if trans success, false if trans failed
 */
bool TransToJsonFromRouteInfo(std::string &jsonStr, const resources::RouteInfo &routeInfo);

void TransToInstanceInfoFromRouteInfo(const resources::RouteInfo &routeInfo, InstanceInfo &instanceInfo);

bool IsLowReliabilityInstance(const resources::InstanceInfo &instanceInfo);

void TransToRouteInfoFromInstanceInfo(const InstanceInfo &instanceInfo, resources::RouteInfo &routeInfo);

FunctionMeta GetFuncMetaFromJson(const std::string &jsonStr);

litebus::Option<std::string> GetFuncName(const std::string &name, const std::string &version,
                                         const std::string &tenantId);

litebus::Option<FunctionMeta> GetFuncMeta(const std::string &funcKey,
                                          const std::unordered_map<std::string, FunctionMeta> &funcMetaMap,
                                          const std::unordered_map<std::string, FunctionMeta> &systemFuncMetaMap);

void GetFuncMounts(MountConfig &mountConfig, const nlohmann::json &funcMounts);

void GetEntryFileAndHandler(FunctionMeta &funcMeta, const nlohmann::json &j);

void LoadLocalFuncMeta(std::unordered_map<std::string, FunctionMeta> &map, const std::string &path);

Layer ParseDelegateDownloadInfo(const nlohmann::json &parser);

litebus::Option<Layer> ParseDelegateDownloadInfoByStr(const std::string &str);

std::vector<Layer> ParseDelegateDownloadInfos(const std::string &str);

std::string GetDeployDir();

}  // namespace functionsystem
#endif
