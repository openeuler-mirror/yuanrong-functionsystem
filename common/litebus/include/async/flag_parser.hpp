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

#ifndef __LITEBUS_FLAG_PARSE_HPP__
#define __LITEBUS_FLAG_PARSE_HPP__

#include <functional>
#include <map>
#include <string>

#include "async/common.hpp"
#include "async/option.hpp"
#include "async/try.hpp"

namespace litebus {

namespace flag {

class FlagParser {
public:
    FlagParser()
    {
        AddFlag(&FlagParser::help, "help", "print usage message", false);
    }

    virtual ~FlagParser()
    {
    }

    // only support read flags from command line
    Option<std::string> ParseFlags(int argc, const char *const *argv, bool supportUnknown = false,
                                   bool supportDuplicate = false);
    std::string Usage(const Option<std::string> &usgMsg = None()) const;

    template <typename Flags, typename T1, typename T2>
    void AddFlag(T1 *t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2);
    template <typename Flags, typename T1, typename T2>
    void AddFlag(T1 *t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2);

    // non-Option type fields in class
    template <typename Flags, typename T1, typename T2>
    void AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2);

    template <typename Flags, typename T1, typename T2>
    void AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2);

    template <typename Flags, typename T>
    void AddFlag(T Flags::*t, const std::string &flagName, const std::string &helpInfo);

    // Option-type fields
    template <typename Flags, typename T>
    void AddFlag(Option<T> Flags::*t, const std::string &flagName, const std::string &helpInfo);

    // non-Option type fields in class
    template <class Flags, class T1, class T2, class Func = std::function<bool(const std::string &, T1 &t1)>>
    void AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 *t2,
                 Func checker);

    template <class Flags, class T1, class T2, class Func = std::function<bool(const std::string &, T1 &t1)>>
    void AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, const T2 &t2,
                 Func checker);

    template <class Flags, class T1, class Func = std::function<bool(const std::string &, T1 &t1)>>
    void AddFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, bool,
                 Func checker);

    bool help{ false };

protected:
    template <typename Flags>
    void AddFlag(std::string Flags::*t1, const std::string &flagName, const std::string &helpInfo, const char *t2)
    {
        AddFlag(t1, flagName, helpInfo, std::string(t2));
    }

    std::string binName;
    Option<std::string> usageMsg;

private:
    struct FlagInfo {
        std::string flagName;
        bool isRequired;
        bool isBoolean;
        std::string helpInfo;
        bool isParsed;
        std::function<Option<Nothing>(FlagParser *, const std::string &)> parse;
    };

    inline void AddFlag(const FlagInfo &flag);

    // construct a temporary flag
    template <typename Flags, typename T>
    void ConstructFlag(Option<T> Flags::*t, const std::string &flagName, const std::string &helpInfo, FlagInfo &flag);

    // construct a temporary flag
    template <typename Flags, typename T1>
    void ConstructFlag(T1 Flags::*t1, const std::string &flagName, const std::string &helpInfo, FlagInfo &flag);

    // convert to string
    template <typename Flags, typename T>
    Option<std::string> ConvertToString(T Flags::*t, const FlagParser &base);

    Option<std::string> InnerParseFlag(const std::pair<std::string, Option<std::string>> &keyValue, bool &opaque,
                                       bool supportUnknow = false);

    Option<std::string> InnerParseFlags(std::multimap<std::string, Option<std::string>> &values,
                                        bool supportUnknow = false, bool supportDuplicate = false);

    Option<std::string> CheckParseArgs(int argc, const char *const *argv) const;

    std::map<std::string, FlagInfo> flags;
};

template <class T, class Func = std::function<bool(const std::string &, T &t)>>
Func NumCheck(T min, T max)
{
    return [min, max](const std::string &flagName, T &t) -> bool {
        auto isValid = (t >= min && t <= max);
        if (!isValid) {
            std::cerr << "flag: " << flagName << " value: " << t << " is out of range. [" << min << ", " << max << "]"
                      << std::endl;
        }
        return isValid;
    };
}

std::function<bool(const std::string &, std::string &)> RealPath();
}    // namespace flag
}    // namespace litebus

#endif /* FLAG_PARSE_HPP__ */
