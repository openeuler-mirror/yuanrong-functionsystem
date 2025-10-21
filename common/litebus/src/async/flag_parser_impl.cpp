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

#include <iostream>

#include "async/flag_parser.hpp"
#include "async/option.hpp"
#include "async/try.hpp"
#include "utils/string_utils.hpp"
#include "utils/os_utils.hpp"
#include "async/flag_parser_impl.hpp"

namespace litebus {

namespace flag {

// forward declaration
struct FlagInfo;
using std::string;

Option<std::string> FlagParser::CheckParseArgs(int argc, const char *const *argv) const
{
    const int ARGS_MAX_NUM = 2048;
    if (argc > ARGS_MAX_NUM) {
        std::string errLog = "Failed: args number is beyond 2048";
        return errLog;
    }

    const size_t ARGS_MAX_CAPS = 104857600;    // 100M
    size_t argsTotalCaps = 0;
    size_t argsTmpCaps = 0;
    for (int i = 0; i < argc; i++) {
        std::string argvTmp = argv[i];
        argsTmpCaps = argsTotalCaps;
        argsTotalCaps += argvTmp.size();
        if (argsTotalCaps - argvTmp.size() != argsTmpCaps) {
            std::string errLog = "Failed: args overflow";
            return errLog;
        }
        if (argsTotalCaps > ARGS_MAX_CAPS) {
            std::string errLog = "Failed: args total capacity is beyond 100Mb";
            return errLog;
        }
    }
    return None();
}

// parse flags read from command line
Option<std::string> FlagParser::ParseFlags(int argc, const char *const *argv, bool supportUnknown, bool)
{
    Option<std::string> ret = CheckParseArgs(argc, argv);
    if (ret.IsSome()) {
        return ret.Get();
    }

    const int FLAG_PREFIX_LEN = 2;
    // Get binary name
    binName = litebus::os::GetFileName(argv[0]);

    std::multimap<std::string, Option<std::string>> keyValues;
    for (int i = 1; i < argc; i++) {
        std::string tmp = argv[i];
        const std::string flagItem(litebus::strings::Trim(tmp));

        // a valid flag should look like this : --no-key or --key or --key=value
        if (flagItem == "--") {
            break;
        }

        if (!litebus::strings::StartsWithPrefix(flagItem, "--")) {
            continue;
        }

        std::string key;
        Option<std::string> value = None();
        size_t pos = flagItem.find_first_of("=");
        // as --no-key or --key, we set key with the characters after '--'
        if (pos == std::string::npos) {
            key = flagItem.substr(FLAG_PREFIX_LEN);
        } else {
            // as --key=value, we set key with the characters from '--' to '=', and set
            // value with the characters after '='
            key = flagItem.substr(FLAG_PREFIX_LEN, pos - FLAG_PREFIX_LEN);
            value = flagItem.substr(pos + 1);
        }
        if (value.IsNone() || value.Get().empty()) {
            continue;
        }

        (void)keyValues.emplace(std::pair<std::string, Option<std::string>>(key, value));
    }

    ret = InnerParseFlags(keyValues, supportUnknown);
    if (ret.IsSome()) {
        return ret.Get();
    }

    return None();
}

// Inner parse function
Option<std::string> FlagParser::InnerParseFlag(const std::pair<std::string, Option<std::string>> &keyValue,
                                               bool &opaque, bool supportUnknow)
{
    const int BOOL_TYPE_FLAG_PREFIX_LEN = 3;
    std::string flagName;
    Option<std::string> flagValue = keyValue.second;
    if (litebus::strings::StartsWithPrefix(keyValue.first, "no-")) {
        flagName = keyValue.first.substr(BOOL_TYPE_FLAG_PREFIX_LEN);
        opaque = true;
    } else {
        flagName = keyValue.first;
    }
    // try to check whether a flag is stored in flags before;
    // if not found then return a string here, means something is wrong.
    auto item = flags.find(flagName);
    if (item == flags.end()) {
        if (!supportUnknow) {
            return string(flagName + " is not a valid flag");
        }
        return None();
    }
    FlagInfo *flag = &(item->second);
    if (flag->isParsed) {
        return "Failed: already parsed flag: " + flagName;
    }
    std::string tmpValue;
    if (!flag->isBoolean) {
        if (opaque) {
            return flagName + " is not a boolean type";
        }

        if (flagValue.IsNone()) {
            return "No value provided for non-boolean type: " + flagName;
        }

        tmpValue = flagValue.Get();
    } else {
        if (flagValue.IsNone() || flagValue.Get() == "") {
            tmpValue = !opaque ? "true" : "false";
        } else if (!opaque) {
            tmpValue = flagValue.Get();
        } else {
            return string("Boolean flag can not have non-empty value");
        }
    }
    // begin to parse value
    Option<Nothing> ret = flag->parse(this, tmpValue);
    if (ret.IsNone()) {
        return "Failed to parse value for: " + flag->flagName;
    }
    flag->isParsed = true;
    return None();
}

// Inner parse function
Option<std::string> FlagParser::InnerParseFlags(std::multimap<std::string, Option<std::string>> &keyValues,
                                                bool supportUnknown, bool)
{
    bool opaque = false;

    for (auto keyValue : keyValues) {
        Option<std::string> flagParserString = InnerParseFlag(keyValue, opaque, supportUnknown);
        if (!flagParserString.IsNone()) {
            return flagParserString.Get();
        }
    }

    // to check flags not given in command line
    // but added as in constructor
    for (auto &flag : flags) {
        if (flag.second.isRequired && flag.second.isParsed == false) {
            return "Error, value of '" + flag.first + "' not provided";
        }
    }

    return None();
}

string &Replaceall(string &str, const string &oldValue, const string &newValue)
{
    for (;;) {
        string::size_type pos(0);
        if ((pos = str.find(oldValue)) != string::npos) {
            (void)str.replace(pos, oldValue.length(), newValue);
        } else {
            break;
        }
    }
    return str;
}

std::string FlagParser::Usage(const Option<std::string> &usgMsg) const
{
    // first line, brief of the usage
    std::string usageString = usgMsg.IsSome() ? usgMsg.Get() + "\n" : "";
    // usage of bin name
    usageString += usageMsg.IsNone() ? "usage: " + binName + " [options]\n" : usageMsg.Get() + "\n";
    // help line of help message, usageLine:message of parametors
    std::string helpLine = "";
    std::string usageLine = "";
    uint32_t i = 0;
    for (auto flag = flags.begin(); flag != flags.end(); ++flag) {
        std::string flagName = flag->second.flagName;
        std::string helpInfo = flag->second.helpInfo;
        // parameter line
        std::string thisLine = flag->second.isBoolean ? " --[no-]" + flagName : " --" + flagName + "=VALUE";
        if (++i < flags.size()) {
            // add paramter help message of each line
            thisLine += " " + helpInfo;
            (void)Replaceall(helpInfo, "\n\r", "\n");
            usageLine += thisLine + "\n";
        } else {
            // breif help message
            helpLine = thisLine + " " + helpInfo + "\n";
        }
    }
    // total usage is brief of usage+ breif of bin + help message + brief of paramters
    return usageString + helpLine + usageLine;
}

std::function<bool(const std::string &, std::string &)> RealPath()
{
    return [](const std::string &flagName, std::string &path) {
        auto real = os::RealPath(path);
        if (real.IsNone()) {
            std::cerr << "flag: " << flagName << " is invalid path. value: " << path << std::endl;
            return false;
        }
        path = real.Get();
        return true;
    };
}
}    // namespace flag
}    // namespace litebus
