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

#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_PERF_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_PERF_H
#include <chrono>
#include <memory>
#include <sstream>

#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"

namespace functionsystem::busproxy {
using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

inline std::string GetDuration(TimePoint &end, TimePoint &start)
{
    return std::to_string(std::chrono::duration<double, std::milli>(end - start).count());
}

struct PerfContext {
    std::string traceID;
    std::string requestID;
    std::string dstInstance;
    std::shared_ptr<TimePoint> grpcReceivedTime;
    std::shared_ptr<TimePoint> proxyReceivedTime;
    std::shared_ptr<TimePoint> proxySendCallTime;
    std::shared_ptr<TimePoint> proxyReceivedCallRspTime;
    std::shared_ptr<TimePoint> grpcReceivedCallResultTime;
    std::shared_ptr<TimePoint> proxyReceivedCallResultTime;
    std::shared_ptr<TimePoint> proxySendCallResultTime;
    std::shared_ptr<TimePoint> proxyReceviedCallResultAckTime;

    void LogPerf()
    {
        std::ostringstream oss;
        /*
        perf|asyn call|send call cost|receive rsp cost|receive result cost|asyn result cost|send result cost|ack cost
        perf|0.1|0.2|0.3|0.5|0.1|0.1|0.1|total|
        */
        oss << "perf";
        // grpc-proxy asyn call
        oss << "|"
            << ((grpcReceivedTime && proxyReceivedTime) ? GetDuration(*proxyReceivedTime, *grpcReceivedTime) : "nil");
        // proxy send call cost
        oss << "|"
            << ((proxyReceivedTime && proxySendCallTime) ? GetDuration(*proxySendCallTime, *proxyReceivedTime) : "nil");
        // receive rsp cost
        oss << "|"
            << ((proxySendCallTime && proxyReceivedCallRspTime)
                    ? GetDuration(*proxyReceivedCallRspTime, *proxySendCallTime)
                    : "nil");
        if (grpcReceivedCallResultTime) {
            // receive result cost
            oss << "|"
                << ((proxySendCallTime && grpcReceivedCallResultTime)
                        ? GetDuration(*grpcReceivedCallResultTime, *proxySendCallTime)
                        : "nil");
            // grpc-proxy asyn result cost
            oss << "|"
                << ((grpcReceivedCallResultTime && proxyReceivedCallResultTime)
                        ? GetDuration(*proxyReceivedCallResultTime, *grpcReceivedCallResultTime)
                        : "nil");
        } else {
            // receive result cost
            oss << "|"
                << ((proxySendCallTime && proxyReceivedCallResultTime)
                        ? GetDuration(*proxyReceivedCallResultTime, *proxySendCallTime)
                        : "nil");
            // grpc-proxy asyn result cost
            oss << "|nil";
        }
        // proxy send result cost
        oss << "|"
            << ((proxySendCallResultTime && proxyReceivedCallResultTime)
                    ? GetDuration(*proxySendCallResultTime, *proxyReceivedCallResultTime)
                    : "nil");
        // ack cost
        oss << "||"
            << ((proxySendCallResultTime && proxyReceviedCallResultAckTime)
                    ? GetDuration(*proxyReceviedCallResultAckTime, *proxySendCallResultTime)
                    : "nil");
        // total
        if (grpcReceivedTime && proxySendCallResultTime) {
            auto timeCost = std::chrono::duration<double, std::milli>(*(proxySendCallResultTime) - *(grpcReceivedTime));
            oss << "|total|" << timeCost.count();
        }
        YRLOG_INFO("{}|{}|dstInstance({})|{}", traceID, requestID, dstInstance, oss.str());
    }
};

class Perf {
public:
    Perf() = default;
    ~Perf() = default;

    inline void Record(const runtime::CallRequest &callReq, const std::string &dstInstance,
                       const std::shared_ptr<TimePoint> &time)
    {
        if (!enable_) {
            return;
        }
        if (perfMap_.find(callReq.requestid()) == perfMap_.end()) {
            auto perfCtx = std::make_shared<PerfContext>();
            perfCtx->traceID = callReq.traceid();
            perfCtx->requestID = callReq.requestid();
            perfCtx->dstInstance = dstInstance;
            perfMap_[callReq.requestid()] = perfCtx;
        }
        auto perfCtx = perfMap_[callReq.requestid()];
        perfCtx->grpcReceivedTime = time;
        perfCtx->proxyReceivedTime = std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
    }

    inline void RecordReceivedCallRsp(const std::string &requestID)
    {
        if (!enable_) {
            return;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            iter->second->proxyReceivedCallRspTime =
                std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
        }
    }

    inline void RecordCallResult(const std::string &requestID, const std::shared_ptr<TimePoint> &time)
    {
        if (!enable_) {
            return;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            iter->second->grpcReceivedCallResultTime = time;
            iter->second->proxyReceivedCallResultTime =
                std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
        }
    }

    inline std::shared_ptr<PerfContext> GetPerfContext(const std::string &requestID)
    {
        if (!enable_) {
            return nullptr;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            return iter->second;
        }
        return nullptr;
    }

    inline void RecordSendCallResult(const std::string &requestID)
    {
        if (!enable_) {
            return;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            iter->second->proxySendCallResultTime =
                std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
        }
    }

    inline void RecordSendCall(const std::string &requestID)
    {
        if (!enable_) {
            return;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            iter->second->proxySendCallTime = std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
        }
    }

    inline void EndRecord(const std::string &requestID)
    {
        if (!enable_) {
            return;
        }
        if (auto iter = perfMap_.find(requestID); iter != perfMap_.end()) {
            iter->second->proxyReceviedCallResultAckTime =
                std::make_shared<TimePoint>(std::chrono::high_resolution_clock::now());
            iter->second->LogPerf();
            (void)perfMap_.erase(iter);
        }
    }

    static void Enable(bool enable)
    {
        enable_ = enable;
    }

private:
    inline static std::atomic<bool> enable_{ false };
    std::unordered_map<std::string, std::shared_ptr<PerfContext>> perfMap_;
};
}  // namespace functionsystem::busproxy

#endif  // FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_PERF_H
