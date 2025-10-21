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

#include "service_handler.h"

namespace functionsystem::service_json {

const std::string YR_LIB_HANDLER = "fusion_computation_handler.fusion_computation_handler";

std::map<std::string, int> YrLibBuilder::ExtendedTimeout(const FunctionConfig &)
{
    return {};
}

std::string YrLibBuilder::Handler()
{
    if (runtime_ == CPP_RUNTIME_VERSION) {
        return "";
    }
    return YR_LIB_HANDLER;
}

std::map<std::string, std::string> YrLibBuilder::HookHandler(const FunctionConfig &functionConfig)
{
    std::map<std::string, std::string> hookHandler;
    if (functionConfig.runtime == JAVA_RUNTIME_VERSION || functionConfig.runtime == JAVA11_RUNTIME_VERSION) {
        hookHandler = { { INIT_HANDLER, "com.yuanrong.handler.InitHandler" },
                        { CALL_HANDLER, "com.yuanrong.handler.CallHandler" },
                        { CHECK_POINT_HANDLER,
                          "com.yuanrong.handler.CheckPointHandler" },
                        { RECOVER_HANDLER, "com.yuanrong.handler.RecoverHandler" },
                        { SHUTDOWN_HANDLER, "com.yuanrong.handler.ShutdownHandler" },
                        { SIGNAL_HANDLER, "com.yuanrong.handler.SignalHandler" } };
    } else {
        hookHandler = {
            { INIT_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.initHandler, "yrlib_handler.init") },
            { CALL_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.callHandler, "yrlib_handler.call") },
            { CHECK_POINT_HANDLER, GetDefaultHandler(functionConfig.functionHookHandlerConfig.checkpointHandler,
                                                     "yrlib_handler.checkpoint") },
            { RECOVER_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.recoverHandler, "yrlib_handler.recover") },
            { SHUTDOWN_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.shutdownHandler, "yrlib_handler.shutdown") },
            { SIGNAL_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.signalHandler, "yrlib_handler.signal") },
            { HEALTH_HANDLER,
              GetDefaultHandler(functionConfig.functionHookHandlerConfig.healthHandler, "yrlib_handler.health") }
        };
    }

    for (auto iter = hookHandler.begin(); iter != hookHandler.end();) {
        if (iter->second == "") {
            (void)hookHandler.erase(iter++);
        } else {
            (void)iter++;
        }
    }
    return hookHandler;
}

std::string YrLibBuilder::GetDefaultHandler(const std::string &handler, const std::string &defaultHandler)
{
    if (runtime_ == CPP_RUNTIME_VERSION) {
        return handler;
    }
    if (handler.empty()) {
        return defaultHandler;
    }
    return handler;
}

std::map<std::string, std::string> YrLibBuilder::ExtendedHandler(const FunctionConfig &)
{
    return {};
}

std::shared_ptr<BuildHandlerMap> GetBuilder(const std::string &kind, const std::string &runtime)
{
    if (kind == YR_LIB) {
        return std::make_shared<YrLibBuilder>(runtime);
    }

    YRLOG_ERROR("(funcMeta)the kind({}) isn't supported", kind);
    return nullptr;
}

}  // namespace functionsystem::service_json