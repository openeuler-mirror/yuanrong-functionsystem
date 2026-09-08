/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_DATA_PLANE_GATEWAY_ACTIVITY_SERVICE_H
#define FUNCTIONSYSTEM_DATA_PLANE_GATEWAY_ACTIVITY_SERVICE_H

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "common/proto/pb/posix/data_plane_gateway_activity.grpc.pb.h"
#include "function_proxy/local_scheduler/instance_control/idle/idle_mgr.h"

namespace functionsystem::local_scheduler {

/**
 * Receives one complete active-stream snapshot per gateway epoch over a
 * dedicated node-local UDS. Gateway activity is kept as an independent idle
 * source so it cannot overwrite FunctionProxy traffic or exec-session state.
 * Missing/expired snapshots make activity unknown and pause idle reclamation;
 * they are never interpreted as zero and never stop the data plane.
 */
class DataPlaneGatewayActivityService final
    : public data_plane_gateway_activity::DataPlaneGatewayActivityService::Service {
public:
    explicit DataPlaneGatewayActivityService(std::shared_ptr<IdleMgr> idleMgr,
                                             std::chrono::seconds heartbeatTimeout = std::chrono::seconds(90),
                                             std::function<void()> gatewayEpochCallback = {});
    ~DataPlaneGatewayActivityService() override;

    /**
     * Enable reports only after LocalSchedDriver has completed Sync/Recover
     * and transitioned its actors to ready.
     */
    void EnableAfterProxySync();

    ::grpc::Status ReportActivity(::grpc::ServerContext *context,
                                  const data_plane_gateway_activity::DataPlaneGatewayActivityRequest *request,
                                  data_plane_gateway_activity::DataPlaneGatewayActivityResponse *response) override;

private:
    struct Lease {
        std::string gatewayEpoch;
        uint64_t timestampMs = 0;
        std::chrono::steady_clock::time_point lastSeen;
        std::unordered_map<std::string, size_t> activeStreamCounts;
    };

    void ExpireLeases();
    void CleanupLoop();
    std::unordered_map<std::string, size_t> AggregateCountsLocked() const;

    std::shared_ptr<IdleMgr> idleMgr_;
    std::chrono::seconds heartbeatTimeout_;
    std::function<void()> gatewayEpochCallback_;
    std::atomic<bool> accepting_{ false };
    std::atomic<bool> stopping_{ false };
    std::mutex mutex_;
    std::condition_variable wakeup_;
    std::unordered_map<std::string, Lease> leases_;
    std::thread cleanupThread_;
};

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTIONSYSTEM_DATA_PLANE_GATEWAY_ACTIVITY_SERVICE_H
