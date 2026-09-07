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

#ifndef COMMON_SCHEDULE_PLUGIN_ROUND_ALLOCATION_CONTEXT_H
#define COMMON_SCHEDULE_PLUGIN_ROUND_ALLOCATION_CONTEXT_H

#include <chrono>
#include <queue>
#include <unordered_map>
#include <vector>

#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::schedule_framework {

struct UnitReservation {
    std::string unitID;
    std::string instanceID;
    resource_view::Resources resources;
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    int64_t expireAtMs{ 0 };
    uint64_t mutationSequenceAtReservation{ 0 };
};

// Keeps Domain reservations across consume rounds. BeginRound pins the next
// immutable snapshot and reconciles the mutable overlay against it; scheduling
// successes continue to add or replace reservations during the active round.
class RoundAllocationContext : public PreAllocatedContext {
    static constexpr int64_t DEFAULT_RESERVATION_TIMEOUT_MS = 18000;
    static constexpr size_t DEFAULT_RESERVATION_RESERVE = 1024;
    using Expiry = std::pair<int64_t, std::string>;
    using ExpiryContainer = std::vector<Expiry>;

    static ExpiryContainer MakeExpiryContainer()
    {
        ExpiryContainer container;
        container.reserve(DEFAULT_RESERVATION_RESERVE);
        return container;
    }

public:
    RoundAllocationContext() : expiry_(std::greater<Expiry>(), MakeExpiryContainer())
    {
        reservations_.reserve(DEFAULT_RESERVATION_RESERVE);
        preAllocatedSelectedFunctionAgentMap.reserve(DEFAULT_RESERVATION_RESERVE);
        mutationBuffer_.reserve(DEFAULT_RESERVATION_RESERVE);
    }
    ~RoundAllocationContext() override = default;

    void BeginRound(const resource_view::ScheduleSnapshotPtr &next)
    {
        if (next == nullptr) {
            return;
        }
        const auto now = NowMs();
        Expire(now);
        if (snapshot == next) {
            return;
        }
        if (IsSameLegacyPublication(next)) {
            snapshot = next;
            return;
        }
        const auto previous = snapshot;
        const bool initial = previous == nullptr;
        const bool viewChanged = !initial && previous->viewInitTime != next->viewInitTime;
        const bool placementsChanged = initial || previous->requestPlacements != next->requestPlacements;
        const bool structureChanged = initial || previous->unitIndex != next->unitIndex;
        const bool labelsChanged = initial || previous->ownerLabels != next->ownerLabels;
        bool journalOverflow = false;
        const bool journalCoveredPlacements = ReconcileMutationJournal(next, initial, journalOverflow);
        snapshot = next;
        schedulerLevel = next->schedulerLevel;
        if (initial) {
            allocated.reserve(next->units.size());
            allocatedLabels.reserve(next->units.size());
        }
        if (labelsChanged) {
            allLocalLabels = next->ownerLabels == nullptr ? resource_view::OwnerLabelIndex{} : *next->ownerLabels;
        }
        InvalidateChangedUnits(previous, next, structureChanged || viewChanged);
        // The journal identifies the exact reservations affected by ordinary
        // ADD/DELETE publications, so those changes are reconciled in O(K)
        // above. A full rebuild is reserved for discontinuities that cannot be
        // proven by the journal.
        if (journalOverflow || viewChanged || structureChanged ||
            (placementsChanged && !journalCoveredPlacements)) {
            RebuildOverlay(now);
        }
    }

    void RecordReservation(const std::string &unitID, const std::string &requestID,
                           const resource_view::InstanceInfo &instance)
    {
        RecordReservationImpl(unitID, requestID, instance, instance.resources());
    }

    void RecordReservation(const std::string &unitID, const std::string &requestID,
                           resource_view::InstanceInfo &&instance)
    {
        resource_view::Resources resources;
        resources.Swap(instance.mutable_resources());
        RecordReservationImpl(unitID, requestID, instance, std::move(resources));
    }

    void RemoveReservation(const std::string &instanceID)
    {
        for (auto iter = reservations_.begin(); iter != reservations_.end(); ++iter) {
            if (iter->second.instanceID == instanceID) {
                EraseReservation(iter);
                return;
            }
        }
    }

    bool RemoveReservationByRequest(const std::string &requestID)
    {
        const auto reservation = reservations_.find(requestID);
        if (reservation == reservations_.end()) {
            return false;
        }
        EraseReservation(reservation);
        return true;
    }

    const UnitReservation *FindReservationByRequest(const std::string &requestID) const
    {
        const auto reservation = reservations_.find(requestID);
        return reservation == reservations_.end() ? nullptr : &reservation->second;
    }

    size_t ReservationCount() const
    {
        return reservations_.size();
    }

    size_t OverlayRebuildCount() const
    {
        return overlayRebuildCount_;
    }

    size_t JournalOverflowCount() const
    {
        return journalOverflowCount_;
    }

    uint64_t ConsumedMutationSequence() const
    {
        return lastConsumedMutationSequence_;
    }

    resource_view::ScheduleSnapshotPtr snapshot;

private:
    bool IsSameLegacyPublication(const resource_view::ScheduleSnapshotPtr &next) const
    {
        // Hand-built snapshots have no publication sequence and retain the
        // legacy revision-based identity rule.
        return snapshot != nullptr && snapshot->publicationSequence == 0 &&
               next->publicationSequence == 0 && snapshot->revision == next->revision &&
               snapshot->viewInitTime == next->viewInitTime;
    }

    bool ReconcileMutationJournal(const resource_view::ScheduleSnapshotPtr &next, bool initial,
                                  bool &journalOverflow)
    {
        if (initial) {
            lastConsumedMutationSequence_ = next->requestMutationSequence;
            return false;
        }
        if (next->requestMutationJournal == nullptr ||
            next->requestMutationSequence <= lastConsumedMutationSequence_) {
            return false;
        }
        const bool covered = next->requestMutationJournal->ReadRange(
            lastConsumedMutationSequence_, next->requestMutationSequence, mutationBuffer_);
        if (!covered) {
            journalOverflow = true;
            ++journalOverflowCount_;
        } else {
            for (const auto &mutation : mutationBuffer_) {
                const auto reservation = reservations_.find(mutation.requestID);
                if (reservation != reservations_.end() &&
                    reservation->second.mutationSequenceAtReservation < mutation.sequence) {
                    EraseReservation(reservation);
                }
            }
        }
        lastConsumedMutationSequence_ = next->requestMutationSequence;
        return covered;
    }

    void InvalidateChangedUnits(const resource_view::ScheduleSnapshotPtr &previous,
                                const resource_view::ScheduleSnapshotPtr &next, bool resetAll)
    {
        if (resetAll) {
            ClearEffectiveAllocatable();
            return;
        }
        if (previous->publicationSequence != 0 &&
            previous->publicationSequence == next->parentPublicationSequence) {
            for (const auto index : next->changedUnitIndices) {
                if (index < next->units.size() && next->units[index] != nullptr) {
                    InvalidateEffectiveAllocatable(next->units[index]->id());
                }
            }
            return;
        }
        for (size_t index = 0; index < next->units.size(); ++index) {
            if (previous->units[index] != next->units[index] && next->units[index] != nullptr) {
                InvalidateEffectiveAllocatable(next->units[index]->id());
            }
        }
    }

    void RecordReservationImpl(const std::string &unitID, const std::string &requestID,
                               const resource_view::InstanceInfo &instance,
                               resource_view::Resources resources)
    {
        auto labels = instance.labels().empty() && instance.kvlabels().empty()
                          ? ::google::protobuf::Map<std::string, resource_view::ValueCounter>{}
                          : ToLabelKVs(instance.labels()) + ToLabelKVs(instance.kvlabels());
        if (const auto old = reservations_.find(requestID); old != reservations_.end()) {
            EraseReservation(old);
        }
        // requestID is the normal idempotency key. A repeated instanceID with
        // another request is exceptional; reuse the already-maintained overlay
        // index to detect it and pay a linear scan only on that path.
        if (preAllocatedSelectedFunctionAgentMap.find(instance.instanceid()) !=
            preAllocatedSelectedFunctionAgentMap.end()) {
            for (auto iter = reservations_.begin(); iter != reservations_.end(); ++iter) {
                if (iter->first != requestID && iter->second.instanceID == instance.instanceid()) {
                    EraseReservation(iter);
                    break;
                }
            }
        }
        auto &unit = allocated[unitID].resource;
        AccumulateResources(unit, resources, true);
        if (!labels.empty()) {
            allocatedLabels[unitID] = allocatedLabels[unitID] + labels;
        }
        InvalidateEffectiveAllocatable(unitID);

        const auto timeout = instance.scheduleoption().scheduletimeoutms() == 0
                                 ? DEFAULT_RESERVATION_TIMEOUT_MS
                                 : instance.scheduleoption().scheduletimeoutms();
        UnitReservation reservation;
        reservation.unitID = unitID;
        reservation.instanceID = instance.instanceid();
        reservation.resources = std::move(resources);
        reservation.labels = std::move(labels);
        reservation.expireAtMs = NowMs() + timeout;
        reservation.mutationSequenceAtReservation =
            snapshot == nullptr ? 0 : snapshot->requestMutationSequence;
        const auto expiration = reservation.expireAtMs;
        reservations_.insert_or_assign(requestID, std::move(reservation));
        expiry_.push({ expiration, requestID });
        ++unitReservationCounts_[unitID];
        preAllocatedSelectedFunctionAgentMap[instance.instanceid()] = unitID;
        preAllocatedSelectedFunctionAgentSet.insert(unitID);
    }
    static int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static void AccumulateResources(resource_view::Resources &target,
                                    const resource_view::Resources &delta, bool add)
    {
        if (target.resources().empty()) {
            if (add) {
                target = delta;
            }
            return;
        }
        for (const auto &[name, resource] : delta.resources()) {
            const auto targetResource = target.resources().find(name);
            if (targetResource == target.resources().end() ||
                resource.type() != resource_view::ValueType::Value_Type_SCALAR || !resource.has_scalar() ||
                targetResource->second.type() != resource_view::ValueType::Value_Type_SCALAR ||
                !targetResource->second.has_scalar()) {
                target = add ? target + delta : target - delta;
                return;
            }
        }
        for (const auto &[name, resource] : delta.resources()) {
            auto &value = *target.mutable_resources()->at(name).mutable_scalar();
            const auto left = resource_view::ToLong(value.value());
            const auto right = resource_view::ToLong(resource.scalar().value());
            value.set_value(resource_view::ToDouble(add ? left + right : left - right));
        }
    }

    void Expire(int64_t now)
    {
        while (!expiry_.empty() && expiry_.top().first <= now) {
            const auto [expireAt, requestID] = expiry_.top();
            expiry_.pop();
            const auto reservation = reservations_.find(requestID);
            if (reservation != reservations_.end() && reservation->second.expireAtMs == expireAt) {
                EraseReservation(reservation);
            }
        }
    }

    void EraseReservation(std::unordered_map<std::string, UnitReservation>::iterator reservation)
    {
        const auto unitID = reservation->second.unitID;
        const auto instanceID = reservation->second.instanceID;
        if (auto allocatedIter = allocated.find(unitID); allocatedIter != allocated.end()) {
            AccumulateResources(allocatedIter->second.resource, reservation->second.resources, false);
        }
        if (!reservation->second.labels.empty()) {
            if (auto labelsIter = allocatedLabels.find(unitID); labelsIter != allocatedLabels.end()) {
                labelsIter->second = labelsIter->second - reservation->second.labels;
            }
        }
        preAllocatedSelectedFunctionAgentMap.erase(instanceID);
        if (auto count = unitReservationCounts_.find(unitID); count != unitReservationCounts_.end()) {
            if (--count->second == 0) {
                unitReservationCounts_.erase(count);
                preAllocatedSelectedFunctionAgentSet.erase(unitID);
                allocated.erase(unitID);
                allocatedLabels.erase(unitID);
            }
        }
        InvalidateEffectiveAllocatable(unitID);
        reservations_.erase(reservation);
    }

    void RebuildOverlay(int64_t now)
    {
        ++overlayRebuildCount_;
        allocated.clear();
        allocatedLabels.clear();
        preAllocatedSelectedFunctionAgentMap.clear();
        preAllocatedSelectedFunctionAgentSet.clear();
        unitReservationCounts_.clear();
        ClearEffectiveAllocatable();

        const auto &placements = snapshot->requestPlacements;
        for (auto iter = reservations_.begin(); iter != reservations_.end();) {
            const auto &reservation = iter->second;
            const bool confirmed = placements != nullptr &&
                                   placements->find(iter->first) != placements->end();
            if (reservation.expireAtMs <= now || confirmed || snapshot->FindUnit(reservation.unitID) == nullptr) {
                iter = reservations_.erase(iter);
                continue;
            }
            auto &unit = allocated[reservation.unitID].resource;
            AccumulateResources(unit, reservation.resources, true);
            if (!reservation.labels.empty()) {
                allocatedLabels[reservation.unitID] = allocatedLabels[reservation.unitID] + reservation.labels;
            }
            preAllocatedSelectedFunctionAgentMap[reservation.instanceID] = reservation.unitID;
            preAllocatedSelectedFunctionAgentSet.insert(reservation.unitID);
            ++unitReservationCounts_[reservation.unitID];
            ++iter;
        }
    }

    std::unordered_map<std::string, UnitReservation> reservations_;
    std::unordered_map<std::string, size_t> unitReservationCounts_;
    std::vector<resource_view::RequestMutation> mutationBuffer_;
    std::priority_queue<Expiry, ExpiryContainer, std::greater<Expiry>> expiry_;
    size_t overlayRebuildCount_{ 0 };
    uint64_t lastConsumedMutationSequence_{ 0 };
    size_t journalOverflowCount_{ 0 };
};

}  // namespace functionsystem::schedule_framework

#endif  // COMMON_SCHEDULE_PLUGIN_ROUND_ALLOCATION_CONTEXT_H
