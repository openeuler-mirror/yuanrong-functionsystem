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

#ifndef __LITEBUS_FLAG_PARSER_IMPL_HPP__
#define __LITEBUS_FLAG_PARSER_IMPL_HPP__

#include <iostream>

#include "async/flag_parser.hpp"
#include "async/option.hpp"
#include "async/try.hpp"
#include "utils/string_utils.hpp"

namespace litebus {
namespace internal {
template <typename T>
inline Option<T> GenericParseValue(const std::string &value)
{
    T ret;
    std::istringstream input(value);
    input >> ret;

    if (input && input.eof()) {
        return ret;
    }

    return None();
}

template <>
inline Option<std::string> GenericParseValue(const std::string &value)
{
    return value;
}

template <>
inline Option<bool> GenericParseValue(const std::string &value)
{
    if (value == "true") {
        return true;
    } else if (value == "false") {
        return false;
    }

    return None();
}

}    // namespace internal

namespace flag {

// forward declaration
struct FlagInfo;

// convert to string
template <typename Flags, typename T>
Option<std::string> ConvertToString(T Flags::*t, const FlagParser &baseFlag)
{
    const Flags *flag = dynamic_cast<Flags *>(&baseFlag);
    if (flag != nullptr) {
        return std::to_string(flag->*t);
    }

    return None();
}

// construct for a Option-type flag
template <typename Flags, typename T>
void FlagParser::ConstructFlag(Option<T> Flags::*, const std::string &flagName, const std::string &helpInfo,
                               FlagInfo &flag)
{
    flag.flagName = flagName;
    flag.helpInfo = helpInfo;
    flag.isBoolean = typeid(T) == typeid(bool);
    flag.isParsed = false;
}

// construct a temporary flag
template <typename Flags, typename T>
void FlagParser::ConstructFlag(T Flags::*, const std::string &flagName, const std::string &helpInfo, FlagInfo &flag)
{
    flag.flagName = flagName;
    flag.helpInfo = helpInfo;
    flag.isBoolean = typeid(T) == typeid(bool);
    flag.isParsed = false;
}

inline void FlagParser::AddFlag(const FlagInfo &flagItem)
{
    flags[flagItem.flagName] = flagItem;
}

template <typename Flags, typename T>
void FlagParser::AddFlag(T Flags::*t, const std::string &flagName, const std::string &helpInfo)
{
    AddFlag(t, flagName, helpInfo, static_cast<const T *>(nullptr));
}

template <typename Flags, typename T1, typename T2>
void FlagParser::AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2)
{
    AddFlag(t1, flagName, helpInfo, &t2);
}

// just for test
template <typename Flags, typename T1, typename T2>
void AddFlag(T1 *t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2)
{
    AddFlag(t1, flagName, helpInfo, &t2);
}

template <typename Flags, typename T1, typename T2>
void FlagParser::AddFlag(T1 *t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2)
{
    if (t1 == nullptr) {
        return;
    }

    FlagInfo flagItem;

    // flagItem is as a output parameter
    ConstructFlag(t1, flagName, helpInfo, flagItem);
    flagItem.parse = [t1](FlagParser *base, const std::string &value) -> Option<Nothing> {
        if (base != nullptr) {
            Option<T1> ret = litebus::internal::GenericParseValue<T1>(value);
            if (ret.IsNone()) {
                return None();
            } else {
                *t1 = ret.Get();
            }
        }

        return Nothing();
    };

    if (t2 != nullptr) {
        flagItem.isRequired = false;
        *t1 = *t2;
    }

    flagItem.helpInfo +=
        helpInfo.size() > 0 && helpInfo.find_last_of("\n\r") != helpInfo.size() - 1 ? " (default: " : "(default: ";
    if (t2) {
        flagItem.helpInfo += litebus::strings::ToString(*t2).Get();
    }
    flagItem.helpInfo += ")";

    // add this flag to a std::map
    AddFlag(flagItem);
}

template <typename Flags, typename T1, typename T2>
void FlagParser::AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2)
{
    if (t1 == nullptr) {
        return;
    }

    Flags *flag = dynamic_cast<Flags *>(this);
    if (flag == nullptr) {
        return;
    }

    FlagInfo flagItem;

    // flagItem is as a output parameter
    ConstructFlag(t1, flagName, helpInfo, flagItem);
    flagItem.parse = [t1](FlagParser *base, const std::string &value) -> Option<Nothing> {
        Flags *flag = dynamic_cast<Flags *>(base);
        if (base != nullptr) {
            Option<T1> ret = litebus::internal::GenericParseValue<T1>(value);
            if (ret.IsNone()) {
                return None();
            } else {
                flag->*t1 = ret.Get();
            }
        }

        return Nothing();
    };

    if (t2 != nullptr) {
        flagItem.isRequired = false;
        flag->*t1 = *t2;
    } else {
        flagItem.isRequired = true;
    }

    flagItem.helpInfo +=
        helpInfo.size() > 0 && helpInfo.find_last_of("\n\r") != helpInfo.size() - 1 ? " (default: " : "(default: ";
    if (t2) {
        flagItem.helpInfo += litebus::strings::ToString(*t2).Get();
    }
    flagItem.helpInfo += ")";

    // add this flag to a std::map
    AddFlag(flagItem);
}

// option-type add flag
template <typename Flags, typename T>
void FlagParser::AddFlag(Option<T> Flags::*t, const std::string &flagName, const std::string &helpInfo)
{
    if (t == nullptr) {
        return;
    }

    Flags *flag = dynamic_cast<Flags *>(this);
    if (flag == nullptr) {
        return;
    }

    FlagInfo flagItem;
    // flagItem is as a output parameter
    ConstructFlag(t, flagName, helpInfo, flagItem);
    flagItem.isRequired = false;
    flagItem.parse = [t](FlagParser *base, const std::string &value) -> Option<Nothing> {
        Flags *flag = dynamic_cast<Flags *>(base);
        if (base != nullptr) {
            Option<T> ret = litebus::internal::GenericParseValue<T>(value);
            if (ret.IsNone()) {
                return None();
            } else {
                flag->*t = Some(ret.Get());
            }
        }

        return Nothing();
    };

    // add this flag to a std::map
    AddFlag(flagItem);
}

template <class Flags, class T1, class T2, class Func>
void FlagParser::AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2,
                         Func checker)
{
    if (t1 == nullptr) {
        return;
    }

    Flags *flag = dynamic_cast<Flags *>(this);
    if (flag == nullptr) {
        return;
    }

    FlagInfo flagItem;
    // flagItem is as a output parameter
    ConstructFlag(t1, flagName, helpInfo, flagItem);
    flagItem.parse = [t1, flagName, checker](FlagParser *base, const std::string &value) -> Option<Nothing> {
        Flags *flag = dynamic_cast<Flags *>(base);
        if (!base) {
            return Nothing();
        }
        Option<T1> ret = litebus::internal::GenericParseValue<T1>(value);
        if (ret.IsNone()) {
            return None();
        }
        flag->*t1 = ret.Get();
        if (!checker) {
            return Nothing();
        }
        if (!checker(flagName, flag->*t1)) {
            return None();
        }
        return Nothing();
    };

    flagItem.isRequired = true;

    if (t2) {
        flagItem.isRequired = false;
        flag->*t1 = *t2;
        flagItem.helpInfo +=
            helpInfo.size() > 0 && helpInfo.find_last_of("\n\r") != helpInfo.size() - 1 ? " (default: " : "(default: ";
        flagItem.helpInfo += litebus::strings::ToString(*t2).Get();
        flagItem.helpInfo += ")";
    }
    // add this flag to a std::map
    AddFlag(flagItem);
}

template <class Flags, class T1, class T2, class Func>
void FlagParser::AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2,
                         Func checker)
{
    AddFlag(t1, flagName, helpInfo, &t2, checker);
}

template <class Flags, class T1, class Func>
void FlagParser::AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, bool,
                         Func checker)
{
    // isRequired is using for template match
    AddFlag(t1, flagName, helpInfo, static_cast<const T1 *>(nullptr), checker);
}
}    // namespace flag
}    // namespace litebus
#endif
