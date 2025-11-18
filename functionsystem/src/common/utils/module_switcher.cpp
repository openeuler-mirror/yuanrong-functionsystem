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
#include "module_switcher.h"

#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utils/os_utils.hpp>

#include "common/logs/logging.h"
#include "common/utils/exception.h"

namespace functionsystem {

namespace LogsApi = observability::api::logs;

ModuleSwitcher::ModuleSwitcher(const std::string &componentName, const std::string &nodeID)
    : componentName_(componentName), nodeID_(nodeID)
{
}

void ModuleSwitcher::SetStop()
{
    stopSignal_->SetValue(true);
}

void ModuleSwitcher::WaitStop()
{
    stopSignal_->GetFuture().Wait();
    stopSignal_ = nullptr;
}

bool ModuleSwitcher::InitLiteBus(const std::string &address, int32_t threadNum, bool enableUDP)
{
    YRLOG_INFO("initialize LiteBus with address: {}, threadNum: {}", address, threadNum);
    auto result = litebus::Initialize("tcp://" + address, "", enableUDP ? "udp://" + address : "", "", threadNum);
    if (result != BUS_OK) {
        YRLOG_ERROR("HTTP server start failed: LiteBus initialize failed, address: {}, result: {}", address, result);
        return false;
    }
    liteBusInitialized_ = true;
    return true;
}

void ModuleSwitcher::FinalizeLiteBus()
{
    if (liteBusInitialized_) {
        litebus::TerminateAll();
        litebus::Finalize();
        liteBusInitialized_ = false;
    }
    YRLOG_INFO("success to stop LiteBus");
}

bool ModuleSwitcher::RegisterHandler(sighandler_t handler, std::shared_ptr<litebus::Promise<bool>> &stopSignal)
{
    RegisterGracefulExit(handler);
    RegisterSigHandler();

    stopSignal = std::make_shared<litebus::Promise<bool>>();
    if (stopSignal == nullptr) {
        YRLOG_ERROR("failed to register stop signal");
        return false;
    }
    stopSignal_ = stopSignal;
    return true;
}

void ModuleSwitcher::InitMetrics(const bool enable, const std::string &config, const std::string &configFile,
                                 const SSLCertConfig &sslCertConfig)
{
    if (!enable || (config.empty() && configFile.empty())) {
        YRLOG_DEBUG("metrics is disabled or config is none");
        return;
    }
    functionsystem::metrics::MetricsAdapter::GetInstance().SetContextAttr("node_id", nodeID_);
    functionsystem::metrics::MetricsAdapter::GetInstance().SetContextAttr("component_name", componentName_);

    if (!config.empty()) {
        try {
            auto confJson = nlohmann::json::parse(config);
            functionsystem::metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(
                confJson, [this](std::string backendName) { return GetMetricsFilesName(backendName); }, sslCertConfig);
            return;
        } catch (nlohmann::detail::parse_error &e) {
            YRLOG_ERROR("parse config json failed, error: {}", e.what());
            return;
        } catch (std::exception &e) {
            YRLOG_ERROR("parse config json failed, error: {}", e.what());
            return;
        }
    }
    auto opt = litebus::os::RealPath(configFile);
    if (opt.IsNone()) {
        YRLOG_ERROR("config json file path invalid. {}", configFile);
        return;
    }
    std::ifstream f(opt.Get());
    try {
        nlohmann::json confJson = nlohmann::json::parse(f);
        metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(
            confJson, [this](std::string backendName) { return GetMetricsFilesName(backendName); }, sslCertConfig);
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("parse config file failed, error: {}", e.what());
    } catch (std::exception &e) {
        YRLOG_ERROR("parse config file failed, error: {}", e.what());
    }
    f.close();
}

void ModuleSwitcher::StopLogger()
{
    auto null = std::make_shared<LogsApi::NullLoggerProvider>();
    LogsApi::Provider::SetLoggerProvider(null);
    logManager_->StopRollingCompress();
}

void ModuleSwitcher::ParseLoggerPattern(const std::string &logConf, observability::api::logs::LogParam &logParam)
{
    try {
        auto confJson = nlohmann::json::parse(logConf);
        if (confJson.find("pattern") == confJson.end()) {
            return;
        }
        auto jpattern = confJson.at("pattern");
        std::string separator = "]";
        if (jpattern.find("separator") != jpattern.end()) {
            separator = jpattern.at("separator").get<std::string>();
        }
        if (jpattern.find("placeholders") == jpattern.end()) {
            return;
        }
        auto jplaceholders = jpattern.at("placeholders");
        std::string pattern = "";
        for (auto &[index, placeholders] : jplaceholders.items()) {
            for (auto &[k, v] : placeholders.items()) {
                std::string value = v.get<std::string>();
                std::cout << "add placeholder " << index << ", key: " << k << ", value: " << value << std::endl;
                if (k == "flags") {
                    pattern = pattern + value + separator;
                } else if (k == "env") {
                    auto e = litebus::os::GetEnv(value);
                    if (e.IsSome()) {
                        pattern = pattern + e.Get() + separator;
                    } else {
                        pattern = pattern + separator;
                    }
                }
            }
        }
        pattern = pattern + "%v";
        logParam.pattern = pattern;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        YR_EXIT(e.what());
    }
}

void ModuleSwitcher::CleanMetrics()
{
    functionsystem::metrics::MetricsAdapter::GetInstance().CleanMetrics();
}

std::string ModuleSwitcher::GetMetricsFilesName(const std::string &backendName)
{
    std::cout << "backendName: " << backendName << std::endl;
    return nodeID_ + "-" + componentName_ + "-metrics.data";
}

}  // namespace functionsystem