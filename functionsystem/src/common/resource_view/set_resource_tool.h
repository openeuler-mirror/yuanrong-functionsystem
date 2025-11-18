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

#ifndef COMMON_RESOURCE_VIEW_SET_RESOURCE_TOOL_H
#define COMMON_RESOURCE_VIEW_SET_RESOURCE_TOOL_H

#include <numeric>
#include <set>
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "resource_type.h"
#include "resource_tool.h"

namespace functionsystem::resource_view {

    template <typename T>
    inline std::string Join(const T &str, const std::string delimiter)
    {
        return std::accumulate(
            str.begin(), str.end(), std::string(),
            [&delimiter](const std::string &x, const std::string &y) { return x.empty() ? y : x + delimiter + y; });
    }

    template <typename T>
    inline std::string CommaSepStr(const T &str)
    {
        return Join<T>(str, ",");
    }

    inline std::string SetValueToString(const Resource &resource)
    {
        ASSERT_FS(resource.type() == ValueType::Value_Type_SET && resource.has_set());

        std::string s = "{ name:" + resource.name() + ", items:" + CommaSepStr(resource.set().items());
        std::string des;
        for (auto const &[key, val]: resource.heterogeneousinfo()) {
            des.append(", " + key + ":" + val);
        }
        return s + des + " }";
    }

    inline bool SetValueValidate(const Resource &resource)
    {
        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_LATENCY_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        bool setValid = !resource.has_set();
        for (auto resourceType: heteResourceTypes) {
            bool hasRes = resource.heterogeneousinfo().find(resourceType) != resource.heterogeneousinfo().end();
            setValid = setValid || hasRes;

            if (hasRes && resourceType != resource_view::HETEROGENEOUS_LATENCY_KEY) {
                std::vector<int> res = stringToIntVector(resource.heterogeneousinfo().at(resourceType));
                bool valValid = std::all_of(res.begin(), res.end(), [](int num) { return num >= 0; });
                if (!valValid) {
                    YRLOG_WARN("invalid set value for resource type {}. ", resourceType);
                    return false;
                }
            }
        }
        if (!setValid) {
            YRLOG_WARN("invalid set value. hbm, latency, stream not included.");
        }
        return setValid;
    }

    inline Resource ScalaToSet(const Resource &resource, std::vector<int> allocatedIndexs, int deviceNum)
    {
        // ScalaToSet: if allocatedIndexs is {0,1,3}, deviceNum is 8, Resource is "name: NPU, type: scala, value: 3.0",
        // change Resource to "name: NPU, type: set, heterogeneousInfo{"CARD_NUM, "3,3,0,3,0,0,0,0"}"
        auto r = resource;
        auto resourceNameFields = litebus::strings::Split(resource.name(), "/");
        // name is like: Heterogeneous.NPU.HBM
        ASSERT_FS(resourceNameFields.size() == HETERO_RESOURCE_FIELD_NUM);
        if (resourceNameFields[RESOURCE_IDX] == HETEROGENEOUS_CARDNUM_KEY) {
            resourceNameFields[RESOURCE_IDX]= HETEROGENEOUS_MEM_KEY;
        }

        r.set_name(resourceNameFields[VENDOR_IDX]);

        r.set_type(resource_view::ValueType::Value_Type_SET);

        std::string scala = std::to_string(static_cast<int>(r.scalar().value()));

        std::string str;
        uint32_t curIdx = 0;
        for (int i = 0; i < deviceNum; i++) {
            r.mutable_set()->add_items(std::to_string(i));
            std::string num = "0";
            int deviceId = -1;
            if (curIdx != allocatedIndexs.size()) {
                deviceId = allocatedIndexs[curIdx];
            }
            if (deviceId == i) {
                num = scala;
                curIdx++;
            }
            if (i != deviceNum - 1) {
                str += num + ",";
            } else {
                str += num;
            }
        }
        (*r.mutable_heterogeneousinfo())[resourceNameFields[RESOURCE_IDX]] = str;
        return r;
    }

    inline Resource ScalaToSet(const Resource &resource, int deviceNum)
    {
        std::vector<int> allocatedIndexs;
        for (int i = 0; i < deviceNum; i++) {
            allocatedIndexs.push_back(i);
        }
        return ScalaToSet(resource, allocatedIndexs, deviceNum);
    }

    inline bool SetValueIsEmpty(const Resource &resource)
    {
        return !resource.has_set() || resource.set().items().empty();
    }

    inline void ExtractSetValueAsVector(const Resource &l, const Resource &r,
                                        const std::string& resourceType,
                                        std::vector<int>& leftVec, std::vector<int>& rightVec)
    {
        if (l.heterogeneousinfo().find(resourceType) != l.heterogeneousinfo().end()) {
            leftVec = stringToIntVector(l.heterogeneousinfo().at(resourceType));
        }
        if (r.heterogeneousinfo().find(resourceType) != r.heterogeneousinfo().end()) {
            rightVec = stringToIntVector(r.heterogeneousinfo().at(resourceType));
        }
    }

    inline bool SetValueIsEqual(const Resource &l, const Resource &r)
    {
        ASSERT_FS(l.has_set() && r.has_set() && l.name() == r.name() && l.type() == r.type() &&
                  l.type() == ValueType::Value_Type_SET);

        bool res = std::equal(l.set().items().begin(), l.set().items().end(),
                              r.set().items().begin(), r.set().items().end());

        auto compareVectorEq = [](const std::vector<int>& leftVec,
                                  const std::vector<int>& rightVec)->bool {
            if (leftVec.size() != rightVec.size()) {
                return false;
            }
            bool res = true;
            for (size_t i = 0; i < leftVec.size(); i ++) {
                res = res && (leftVec[i] == rightVec[i]);
            }
            return res;
        };

        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_LATENCY_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        for (auto resourceType: heteResourceTypes) {
            std::vector<int> leftVec, rightVec;
            ExtractSetValueAsVector(l, r, resourceType, leftVec, rightVec);
            res = res && compareVectorEq(leftVec, rightVec);
        }

        return res;
    }

    inline Resource SetValueSub(const Resource &l, const Resource &r)
    {
        ASSERT_FS(l.has_set() && r.has_set() && l.name() == r.name() && l.type() == r.type() &&
                  l.type() == ValueType::Value_Type_SET);
        auto leftResource = l;
        std::set<int> validDevices;
        auto merge_func = [](std::vector<int>& leftVec, std::vector<int>& rightVec)
            ->std::vector<int>& {
            ASSERT_FS(leftVec.size() >= rightVec.size());
            if (rightVec.empty()) {
                return leftVec;
            }
            for (size_t i = 0; i < leftVec.size(); i++) {
                leftVec[i] -= rightVec[i];
            }
            return leftVec;
        };

        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_LATENCY_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        for (auto resourceType: heteResourceTypes) {
            std::vector<int> leftVec;
            std::vector<int> rightVec;
            ExtractSetValueAsVector(l, r, resourceType, leftVec, rightVec);
            auto mergeRes = merge_func(leftVec, rightVec);
            if (!mergeRes.empty()) {
                for (size_t idx = 0; idx < mergeRes.size(); idx++) {
                    if (mergeRes[idx] > 0 && validDevices.find(idx) == validDevices.end()) {
                        validDevices.insert(idx);
                    }
                }
                (*(leftResource.mutable_heterogeneousinfo()))[resourceType] = intVectorToString(mergeRes);
            }
        }
        if (leftResource.set().items().size() == 0 && r.set().items().size() != 0) {
            leftResource.mutable_set()->mutable_items()->CopyFrom(r.set().items());
        }

        return leftResource;
    }

    inline Resource SetValueAdd(const Resource &l, const Resource &r)
    {
        ASSERT_FS(l.has_set() && r.has_set() && l.name() == r.name() && l.type() == r.type() &&
                  l.type() == ValueType::Value_Type_SET);

        auto leftRes = l;
        std::set<int> validDevices;
        auto merge_func = [](std::vector<int> &leftVec, std::vector<int> &rightVec)
            ->std::vector<int>& {
            if (leftVec.empty() || rightVec.empty()) {
                return rightVec.empty() ? leftVec : rightVec;
            }
            ASSERT_FS(leftVec.size() == rightVec.size());
            for (size_t i = 0; i < leftVec.size(); i++) {
                leftVec[i] += rightVec[i];
            }
            return leftVec;
        };

        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_LATENCY_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        for (auto resourceType: heteResourceTypes) {
            std::vector<int> leftVec, rightVec;
            ExtractSetValueAsVector(l, r, resourceType, leftVec, rightVec);
            auto mergeRes = merge_func(leftVec, rightVec);
            if (!mergeRes.empty()) {
                for (size_t idx = 0; idx < mergeRes.size(); idx ++) {
                    auto to_add = idx;
                    if (mergeRes[idx] > 0 && validDevices.find(to_add) == validDevices.end()) {
                        validDevices.insert(to_add);
                    }
                }
                (*(leftRes.mutable_heterogeneousinfo()))[resourceType] = intVectorToString(mergeRes);
            }
        }
        if (leftRes.set().items().size() == 0 && r.set().items().size() != 0) {
            leftRes.mutable_set()->mutable_items()->CopyFrom(r.set().items());
        }
        return leftRes;
    }

    inline bool SetValueLess(const Resource &l, const Resource &r)
    {
        ASSERT_FS(l.has_set() && r.has_set() && l.name() == r.name() && l.type() == r.type() &&
                  l.type() == ValueType::Value_Type_SET);

        // latency is not for comparison but only for scoring.
        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        bool res = true;
        for (auto resourceType: heteResourceTypes) {
            if (l.heterogeneousinfo().find(resourceType) != l.heterogeneousinfo().end() &&
                r.heterogeneousinfo().find(resourceType) != r.heterogeneousinfo().end()) {
                bool curRes = false;
                std::vector<int> leftVec;
                std::vector<int> rightVec;
                ExtractSetValueAsVector(l, r, resourceType, leftVec, rightVec);
                ASSERT_FS(leftVec.size() == rightVec.size());
                for (size_t i = 0; i < leftVec.size(); i ++) {
                    curRes = curRes || (leftVec[i] <= rightVec[i]);
                }
                res = res && curRes;
            }
        }

        return res;
    }

    inline bool SetValueGreater(const Resource &l, const Resource &r)
    {
        ASSERT_FS(l.has_set() && r.has_set() && l.name() == r.name() && l.type() == r.type() &&
                  l.type() == ValueType::Value_Type_SET);

        // latency is not for comparison but only for scoring.
        std::vector<std::string> heteResourceTypes{ resource_view::HETEROGENEOUS_MEM_KEY,
                                                    resource_view::HETEROGENEOUS_STREAM_KEY };
        bool res = true;
        for (auto resourceType: heteResourceTypes) {
            if (l.heterogeneousinfo().find(resourceType) != l.heterogeneousinfo().end() &&
                r.heterogeneousinfo().find(resourceType) != r.heterogeneousinfo().end()) {
                bool curRes = true;
                std::vector<int> leftVec;
                std::vector<int> rightVec;
                ExtractSetValueAsVector(l, r, resourceType, leftVec, rightVec);
                ASSERT_FS(leftVec.size() == rightVec.size());
                for (size_t i = 0; i < leftVec.size(); i ++) {
                    curRes = curRes && (leftVec[i] >= rightVec[i]);
                }
                res = res && curRes;
            }
        }

        return res;
    }

}  // namespace functionsystem::resource_view

#endif  // COMMON_RESOURCE_VIEW_SET_RESOURCE_TOOL_H

