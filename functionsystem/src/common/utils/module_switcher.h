
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

#ifndef COMMON_MODULE_SWITCHER_H
#define COMMON_MODULE_SWITCHER_H

#include "async/future.hpp"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "logs/sdk/log_param_parser.h"

namespace functionsystem {
const size_t MAX_NODE_ID_LENGTH = 24;
const size_t FIX_LENGTH = 12;
class ModuleSwitcher {
public:
    ModuleSwitcher(const std::string &componentName, const std::string &nodeID);
    ~ModuleSwitcher() = default;
    void SetStop();
    void WaitStop();
    bool InitLiteBus(const std::string &address, int32_t threadNum = LITEBUS_THREAD_NUM, bool enableUDP = true);
    void FinalizeLiteBus();
    bool RegisterHandler(sighandler_t handler, std::shared_ptr<litebus::Promise<bool>> &stopSignal);

    template <typename T>
    bool InitLogger(const T &flags)
    {
        namespace LogsApi = observability::api::logs;
        namespace LogsSdk = observability::sdk::logs;
        const std::string &logConf = flags.GetLogConfig();
        std::cout << componentName_ << " log config: " << logConf << std::endl;
        auto globalLogParam = LogsSdk::GetGlobalLogParam(logConf);
        auto lp = std::make_shared<LogsSdk::LoggerProvider>(globalLogParam);
        auto nodeID = flags.GetNodeID();
        if (nodeID.size() > MAX_NODE_ID_LENGTH) {
            nodeID = nodeID.substr(0, FIX_LENGTH) + "-xx-" + nodeID.substr(nodeID.size() - FIX_LENGTH, FIX_LENGTH);
        }
        auto fileName = nodeID + "-" + componentName_;
        auto loggerParam = LogsSdk::GetLogParam(logConf, flags.GetNodeID(), componentName_, false, fileName);
        ParseLoggerPattern(logConf, loggerParam);
        functionsystem::metrics::MetricsAdapter::GetInstance().SetContextAttr("log_dir", loggerParam.logDir);
        lp->CreateYrLogger(loggerParam);
        LogsApi::Provider::SetLoggerProvider(lp);

        logManager_ = std::make_shared<LogsSdk::LogManager>(loggerParam);
        logManager_->StartRollingCompress(LogsSdk::LogRollingCompress);

        return true;
    }
    void StopLogger();
    void ParseLoggerPattern(const std::string &logConf, observability::api::logs::LogParam &logParam);
    void InitMetrics(const bool enable, const std::string &config, const std::string &configFile,
                     const SSLCertConfig &sslCertConfig = {});
    void CleanMetrics();

private:
    std::string GetMetricsFilesName(const std::string &backendName);

private:
    std::string componentName_;
    std::string nodeID_;
    std::atomic_bool liteBusInitialized_;
    std::shared_ptr<litebus::Promise<bool>> stopSignal_{ nullptr };
    std::shared_ptr<observability::sdk::logs::LogManager> logManager_{ nullptr };
};
}  // namespace functionsystem
#endif  // COMMON_MODULE_SWITCHER_H
