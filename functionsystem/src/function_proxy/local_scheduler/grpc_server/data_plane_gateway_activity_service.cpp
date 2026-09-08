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

#include "data_plane_gateway_activity_service.h"

#include <algorithm>
#include <limits>

#include "common/logs/logging.h"

namespace functionsystem::local_scheduler {

namespace {
constexpr int32_t OK_CODE = 0;

}  // namespace

DataPlaneGatewayActivityService::DataPlaneGatewayActivityService(std::shared_ptr<IdleMgr> idleMgr,
                                                                 std::chrono::seconds heartbeatTimeout,
                                                                 std::function<void()> gatewayEpochCallback)
    : idleMgr_(std::move(idleMgr)),
      heartbeatTimeout_(heartbeatTimeout),
      gatewayEpochCallback_(std::move(gatewayEpochCallback))
{
    cleanupThread_ = std::thread([this] { CleanupLoop(); });
}

DataPlaneGatewayActivityService::~DataPlaneGatewayActivityService()
{
    stopping_.store(true);
    wakeup_.notify_all();
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
}

void DataPlaneGatewayActivityService::EnableAfterProxySync()
{
    if (idleMgr_ == nullptr || accepting_.load(std::memory_order_acquire)) {
        return;
    }
    // This call is synchronous. The actor observes UNKNOWN before the gRPC
    // service starts accepting a batch, so a startup report cannot race with
    // the initial pause of idle reclamation.
    idleMgr_->GatewayActivityUnavailable();
    accepting_.store(true, std::memory_order_release);
}

::grpc::Status DataPlaneGatewayActivityService::ReportActivity(
    ::grpc::ServerContext *context, const data_plane_gateway_activity::DataPlaneGatewayActivityRequest *request,
    data_plane_gateway_activity::DataPlaneGatewayActivityResponse *response)
{
    (void)context;
    if (request == nullptr || response == nullptr || request->gateway_epoch().empty() || request->timestamp_ms() == 0) {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid gateway activity snapshot");
    }
    if (!accepting_.load(std::memory_order_acquire)) {
        return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "proxy state synchronization is not complete");
    }
    if (idleMgr_ == nullptr) {
        return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION, "idle manager is unavailable");
    }

    Lease lease;
    lease.gatewayEpoch = request->gateway_epoch();
    lease.timestampMs = request->timestamp_ms();
    lease.lastSeen = std::chrono::steady_clock::now();
    for (const auto &entry : request->activities()) {
        if (entry.instance_id().empty()
            || entry.active_stream_count() > std::numeric_limits<size_t>::max()) {
            return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "invalid gateway activity entry");
        }
        auto [it, inserted] = lease.activeStreamCounts.emplace(
            entry.instance_id(), static_cast<size_t>(entry.active_stream_count()));
        if (!inserted) {
            return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "duplicate gateway activity instance");
        }
    }

    bool isNewGatewayEpoch = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto existing = leases_.find(lease.gatewayEpoch);
        if (existing != leases_.end() && lease.timestampMs < existing->second.timestampMs) {
            response->set_code(OK_CODE);
            response->set_message("stale activity ignored");
            return ::grpc::Status::OK;
        }
        isNewGatewayEpoch = existing == leases_.end();
        // One batch is authoritative for one gateway process epoch. Entries
        // omitted from the next batch are therefore treated as zero.
        leases_[lease.gatewayEpoch] = std::move(lease);
        // Keep reconciliation submission serialized with expiry. This avoids
        // an expired-lease UNKNOWN update overtaking a concurrently accepted
        // complete snapshot after both methods release the service mutex.
        idleMgr_->GatewayActivityReconcile(AggregateCountsLocked());
    }
    if (isNewGatewayEpoch && gatewayEpochCallback_) {
        gatewayEpochCallback_();
    }
    response->set_code(OK_CODE);
    response->set_message("activity accepted");
    return ::grpc::Status::OK;
}

void DataPlaneGatewayActivityService::ExpireLeases()
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    bool availabilityChanged = false;
    for (auto it = leases_.begin(); it != leases_.end();) {
        if (now - it->second.lastSeen >= heartbeatTimeout_) {
            it = leases_.erase(it);
            availabilityChanged = true;
        } else {
            ++it;
        }
    }
    if (!availabilityChanged || idleMgr_ == nullptr) {
        return;
    }
    if (leases_.empty()) {
        YRLOG_WARN("all data plane gateway activity leases expired; pausing idle eviction");
        idleMgr_->GatewayActivityUnavailable();
        return;
    }
    idleMgr_->GatewayActivityReconcile(AggregateCountsLocked());
}

std::unordered_map<std::string, size_t> DataPlaneGatewayActivityService::AggregateCountsLocked() const
{
    std::unordered_map<std::string, size_t> aggregate;
    for (const auto &[gatewayEpoch, lease] : leases_) {
        (void)gatewayEpoch;
        for (const auto &[instanceID, count] : lease.activeStreamCounts) {
            auto &total = aggregate[instanceID];
            total = (std::numeric_limits<size_t>::max() - total < count) ? std::numeric_limits<size_t>::max()
                                                                         : total + count;
        }
    }
    return aggregate;
}

void DataPlaneGatewayActivityService::CleanupLoop()
{
    const auto interval = std::max(std::chrono::seconds(1), heartbeatTimeout_ / 3);
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_.load()) {
        wakeup_.wait_for(lock, interval, [this] { return stopping_.load(); });
        if (stopping_.load()) {
            break;
        }
        lock.unlock();
        ExpireLeases();
        lock.lock();
    }
}

}  // namespace functionsystem::local_scheduler
