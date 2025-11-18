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

#ifndef COMMON_RESOURCE_VIEW_SCALA_RESOURCE_TOOL_H
#define COMMON_RESOURCE_VIEW_SCALA_RESOURCE_TOOL_H

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "resource_type.h"

namespace functionsystem::resource_view {

extern const int32_t THOUSAND_INT;
extern const double THOUSAND_DOUBLE;

inline long long ToLong(double value)
{
    return std::llround(value * THOUSAND_INT);
}
inline double ToDouble(long long value)
{
    auto intPart = value / THOUSAND_INT;
    auto decPart = static_cast<double>(value % THOUSAND_INT) / THOUSAND_DOUBLE;

    return static_cast<double>(intPart) + decPart;
}

inline std::string ScalaValueToString(const Resource &resource)
{
    ASSERT_FS(resource.type() == ValueType::Value_Type_SCALAR && resource.has_scalar());

    std::string s = "{" + resource.name() + ":" + std::to_string(static_cast<int64_t>(resource.scalar().value())) +
                    ":" + std::to_string(static_cast<int64_t>(resource.scalar().limit())) + "}";
    return s;
}

inline bool ScalaValueValidate(const Resource &resource)
{
    if (!resource.has_scalar() || resource.scalar().value() < 0) {
        YRLOG_WARN("invalid scala value : has no scala element({}) or value({}) < 0.", !resource.has_scalar(),
                   resource.has_scalar() ? resource.scalar().value() : 0);
        return false;
    }
    return true;
}

inline bool ScalaValueIsEmpty(const Resource &resource)
{
    return !resource.has_scalar() || (abs(resource.scalar().value()) < EPSINON);
}

inline bool ScalaValueIsEqual(const Resource &l, const Resource &r)
{
    ASSERT_FS(l.has_scalar() && r.has_scalar() && l.name() == r.name() && l.type() == r.type() &&
              l.type() == ValueType::Value_Type_SCALAR);
    return abs(l.scalar().value() - r.scalar().value()) < EPSINON;
}

inline Resource ScalaValueAdd(const Resource &l, const Resource &r)
{
    ASSERT_FS(l.has_scalar() && r.has_scalar() && l.name() == r.name() && l.type() == r.type() &&
              l.type() == ValueType::Value_Type_SCALAR);

    Resource res = l;
    res.mutable_scalar()->set_value(ToDouble(ToLong(l.scalar().value()) + ToLong(r.scalar().value())));
    return res;
}

inline Resource ScalaValueSub(const Resource &l, const Resource &r)
{
    ASSERT_FS(l.has_scalar() && r.has_scalar() && l.name() == r.name() && l.type() == r.type() &&
              l.type() == ValueType::Value_Type_SCALAR);
    Resource res = l;
    res.mutable_scalar()->set_value(ToDouble(ToLong(l.scalar().value()) - ToLong(r.scalar().value())));
    return res;
}

inline bool ScalaValueLess(const Resource &l, const Resource &r)
{
    ASSERT_FS(l.has_scalar() && r.has_scalar() && l.name() == r.name() && l.type() == r.type() &&
              l.type() == ValueType::Value_Type_SCALAR);
    return l.scalar().value() < r.scalar().value();
}

}  // namespace functionsystem::resource_view

#endif  // COMMON_RESOURCE_VIEW_SCALA_RESOURCE_TOOL_H
