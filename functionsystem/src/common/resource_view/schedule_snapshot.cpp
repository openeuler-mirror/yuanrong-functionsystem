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

#include "schedule_snapshot.h"

#include <chrono>

#include <algorithm>
#include <utility>

namespace functionsystem::resource_view {

uint64_t RequestMutationJournal::Append(const std::string &requestID)
{
    if (requestID.empty()) {
        return LatestSequence();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sequence = latestSequence_.load(std::memory_order_relaxed) + 1;
    if (mutations_.size() < capacity_) {
        mutations_.emplace_back(RequestMutation{ sequence, requestID });
    } else {
        auto &slot = mutations_[firstIndex_];
        slot.sequence = sequence;
        slot.requestID = requestID;
        firstIndex_ = (firstIndex_ + 1) % capacity_;
    }
    latestSequence_.store(sequence, std::memory_order_release);
    return sequence;
}

uint64_t RequestMutationJournal::LatestSequence() const
{
    return latestSequence_.load(std::memory_order_acquire);
}

bool RequestMutationJournal::ReadRange(uint64_t afterSequence, uint64_t throughSequence,
                                       std::vector<RequestMutation> &mutations) const
{
    mutations.clear();
    if (afterSequence >= throughSequence) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto latestSequence = latestSequence_.load(std::memory_order_relaxed);
    if (throughSequence > latestSequence || mutations_.empty()) {
        return false;
    }
    const auto oldestSequence = latestSequence - mutations_.size() + 1;
    if (afterSequence < oldestSequence - 1) {
        return false;
    }
    const auto count = static_cast<size_t>(throughSequence - afterSequence);
    mutations.reserve(count);
    for (auto sequence = afterSequence + 1; sequence <= throughSequence; ++sequence) {
        const auto offset = static_cast<size_t>(sequence - oldestSequence);
        const auto index = (firstIndex_ + offset) % mutations_.size();
        const auto &mutation = mutations_[index];
        if (mutation.sequence != sequence) {
            mutations.clear();
            return false;
        }
        mutations.emplace_back(mutation);
    }
    return true;
}

const ResourceUnit *ScheduleSnapshot::FindUnit(const std::string &unitID) const
{
    if (unitIndex == nullptr) {
        return nullptr;
    }
    const auto iter = unitIndex->find(unitID);
    if (iter == unitIndex->end() || iter->second >= units.size()) {
        return nullptr;
    }
    return units[iter->second].get();
}

const MonopolyCandidates *ScheduleSnapshot::FindMonopolyCandidates(const std::string &proportion,
                                                                   const std::string &memory) const
{
    if (monopolyIndex == nullptr) {
        return nullptr;
    }
    const auto proportionIt = monopolyIndex->find(proportion);
    if (proportionIt == monopolyIndex->end()) {
        return nullptr;
    }
    const auto memoryIt = proportionIt->second.find(memory);
    return memoryIt == proportionIt->second.end() ? nullptr : &memoryIt->second;
}

ResourceUnit ScheduleSnapshot::MaterializeResourceView() const
{
    ResourceUnit result;
    if (rootSummary != nullptr) {
        result = *rootSummary;
    }
    result.set_revision(revision);
    result.set_viewinittime(viewInitTime);
    for (const auto &unit : units) {
        if (unit == nullptr) {
            continue;
        }
        (*result.mutable_fragment())[unit->id()] = *unit;
        for (const auto &[instanceID, instance] : unit->instances()) {
            (*result.mutable_instances())[instanceID] = instance;
        }
        for (const auto &[proportion, bucketIndex] : unit->bucketindexs()) {
            auto &rootBucketIndex = (*result.mutable_bucketindexs())[proportion];
            for (const auto &[memory, bucket] : bucketIndex.buckets()) {
                auto &rootBucket = (*rootBucketIndex.mutable_buckets())[memory];
                auto &old = (*rootBucket.mutable_allocatable())[unit->id()];
                auto &total = *rootBucket.mutable_total();
                total.set_sharednum((total.sharednum() - old.sharednum()) + bucket.total().sharednum());
                total.set_monopolynum((total.monopolynum() - old.monopolynum()) +
                                      bucket.total().monopolynum());
                old = bucket.total();
            }
        }
    }
    return result;
}

ScheduleResourceView::ScheduleResourceView(const ResourceViewInfo &legacy) : legacy_(&legacy)
{
}

ScheduleResourceView::ScheduleResourceView(ScheduleSnapshotPtr snapshot)
    : snapshot_(std::move(snapshot))
{
}

bool ScheduleResourceView::IsSnapshot() const
{
    return snapshot_ != nullptr;
}

SCHEDULER_LEVEL ScheduleResourceView::GetSchedulerLevel() const
{
    return snapshot_ == nullptr ? legacy_->schedulerLevel : snapshot_->schedulerLevel;
}

const RequestPlacementIndex &ScheduleResourceView::GetRequestPlacements() const
{
    static const RequestPlacementIndex EMPTY;
    if (snapshot_ != nullptr) {
        return snapshot_->requestPlacements == nullptr ? EMPTY : *snapshot_->requestPlacements;
    }
    return legacy_ == nullptr ? EMPTY : legacy_->alreadyScheduled;
}

const OwnerLabelIndex &ScheduleResourceView::GetOwnerLabels() const
{
    static const OwnerLabelIndex EMPTY;
    if (snapshot_ != nullptr) {
        return snapshot_->ownerLabels == nullptr ? EMPTY : *snapshot_->ownerLabels;
    }
    return legacy_ == nullptr ? EMPTY : legacy_->allLocalLabels;
}

const ResourceUnit *ScheduleResourceView::FindUnit(const std::string &unitID) const
{
    if (snapshot_ != nullptr) {
        return snapshot_->FindUnit(unitID);
    }
    if (legacy_ == nullptr) {
        return nullptr;
    }
    const auto iter = legacy_->resourceUnit.fragment().find(unitID);
    return iter == legacy_->resourceUnit.fragment().end() ? nullptr : &iter->second;
}

const ResourceUnit *ScheduleResourceView::GetLegacyResourceUnit() const
{
    return legacy_ == nullptr ? nullptr : &legacy_->resourceUnit;
}

const ScheduleSnapshotPtr &ScheduleResourceView::GetSnapshot() const
{
    return snapshot_;
}

ResourceUnit ScheduleResourceView::MaterializeResourceView() const
{
    if (snapshot_ != nullptr) {
        return snapshot_->MaterializeResourceView();
    }
    return legacy_ == nullptr ? ResourceUnit{} : legacy_->resourceUnit;
}

ScheduleSnapshotPtr ScheduleSnapshotStore::Load() const
{
    return std::atomic_load_explicit(&published_, std::memory_order_acquire);
}

void ScheduleSnapshotStore::Publish(ScheduleSnapshotPtr next)
{
    std::atomic_store_explicit(&published_, std::move(next), std::memory_order_release);
}

void ScheduleSnapshotStore::EnableDiagnostics(bool enabled)
{
    diagnosticsEnabled_.store(enabled, std::memory_order_relaxed);
}

bool ScheduleSnapshotStore::DiagnosticsEnabled() const
{
    return diagnosticsEnabled_.load(std::memory_order_relaxed);
}

void ScheduleSnapshotStore::RecordBuildDuration(uint64_t nanos)
{
    buildCalls_.fetch_add(1, std::memory_order_relaxed);
    buildTotalNanos_.fetch_add(nanos, std::memory_order_relaxed);
    auto maximum = buildMaxNanos_.load(std::memory_order_relaxed);
    while (maximum < nanos) {
        if (buildMaxNanos_.compare_exchange_weak(maximum, nanos, std::memory_order_relaxed)) {
            break;
        }
    }
}

SnapshotBuildDiagnostics ScheduleSnapshotStore::GetBuildDiagnostics() const
{
    return SnapshotBuildDiagnostics{
        buildCalls_.load(std::memory_order_relaxed),
        buildTotalNanos_.load(std::memory_order_relaxed),
        buildMaxNanos_.load(std::memory_order_relaxed),
    };
}

bool ScheduleSnapshotDirtySet::Empty() const
{
    return unitIDs.empty() && !structure && !requestPlacements && !ownerLabels && !monopolyIndex && !metadata;
}

void ScheduleSnapshotDirtySet::Merge(const ScheduleSnapshotDirtySet &other)
{
    unitIDs.insert(other.unitIDs.begin(), other.unitIDs.end());
    structure = structure || other.structure;
    requestPlacements = requestPlacements || other.requestPlacements;
    ownerLabels = ownerLabels || other.ownerLabels;
    monopolyIndex = monopolyIndex || other.monopolyIndex;
    metadata = metadata || other.metadata;
}

void ScheduleSnapshotDirtySet::Clear()
{
    unitIDs.clear();
    structure = false;
    requestPlacements = false;
    ownerLabels = false;
    monopolyIndex = false;
    metadata = false;
}

ScheduleSnapshotBuilder::ScheduleSnapshotBuilder(std::shared_ptr<ScheduleSnapshotStore> store)
    : store_(std::move(store)),
      requestMutationJournal_(std::make_shared<RequestMutationJournal>())
{
}

uint64_t ScheduleSnapshotBuilder::RecordRequestMutation(const std::string &requestID)
{
    return requestMutationJournal_->Append(requestID);
}

std::shared_ptr<const MonopolyPrefilterIndex> ScheduleSnapshotBuilder::BuildMonopolyIndex(const ResourceUnit &view)
{
    auto index = std::make_shared<MonopolyPrefilterIndex>();
    for (const auto &[proportion, bucketIndex] : view.bucketindexs()) {
        auto &memoryIndex = (*index)[proportion];
        for (const auto &[memory, bucket] : bucketIndex.buckets()) {
            auto &candidates = memoryIndex[memory];
            candidates.total = bucket.total().monopolynum();
            candidates.unitIDs.reserve(bucket.allocatable_size());
            for (const auto &[unitID, info] : bucket.allocatable()) {
                if (info.monopolynum() > 0) {
                    candidates.unitIDs.emplace_back(unitID);
                }
            }
        }
    }
    return index;
}

std::shared_ptr<const MonopolyPrefilterIndex> ScheduleSnapshotBuilder::UpdateMonopolyIndex(
    const ResourceUnit &view, const ScheduleSnapshotPtr &current,
    const ScheduleSnapshotDirtySet &dirtySet)
{
    if (current == nullptr || current->monopolyIndex == nullptr || dirtySet.structure) {
        return BuildMonopolyIndex(view);
    }
    auto index = std::make_shared<MonopolyPrefilterIndex>(*current->monopolyIndex);
    for (const auto &[proportion, bucketIndex] : view.bucketindexs()) {
        auto &memoryIndex = (*index)[proportion];
        for (const auto &[memory, bucket] : bucketIndex.buckets()) {
            auto &candidates = memoryIndex[memory];
            candidates.total = bucket.total().monopolynum();
            for (const auto &unitID : dirtySet.unitIDs) {
                const auto allocatable = bucket.allocatable().find(unitID);
                const bool shouldContain = allocatable != bucket.allocatable().end() &&
                                           allocatable->second.monopolynum() > 0;
                const auto candidate = std::find(candidates.unitIDs.begin(), candidates.unitIDs.end(), unitID);
                if (shouldContain && candidate == candidates.unitIDs.end()) {
                    candidates.unitIDs.emplace_back(unitID);
                } else if (!shouldContain && candidate != candidates.unitIDs.end()) {
                    candidates.unitIDs.erase(candidate);
                }
            }
        }
    }
    return index;
}

std::shared_ptr<const ResourceUnit> ScheduleSnapshotBuilder::BuildRootSummary(const ResourceUnit &view)
{
    // Do not copy the whole protobuf and clear fragment/instances afterwards:
    // protobuf copy is deep, so that would copy every Unit on every snapshot
    // publication. Copy only root-level fields used by the compatibility path.
    auto summary = std::make_shared<ResourceUnit>();
    summary->set_id(view.id());
    *summary->mutable_capacity() = view.capacity();
    *summary->mutable_allocatable() = view.allocatable();
    *summary->mutable_actualuse() = view.actualuse();
    *summary->mutable_nodelabels() = view.nodelabels();
    *summary->mutable_systeminfo() = view.systeminfo();
    summary->set_maxinstancenum(view.maxinstancenum());
    // Bucket allocatable maps scale with Unit count. Reconstruct them only for
    // the rare legacy-compatibility materialization instead of copying them on
    // every snapshot publication. The normal snapshot path uses monopolyIndex.
    summary->set_revision(view.revision());
    summary->set_status(view.status());
    summary->set_alias(view.alias());
    summary->set_ownerid(view.ownerid());
    summary->set_viewinittime(view.viewinittime());
    return summary;
}

void ScheduleSnapshotBuilder::RebuildUnits(const ResourceUnit &view, const ScheduleSnapshotPtr &current,
                                           const ScheduleSnapshotDirtySet &dirtySet,
                                           ScheduleSnapshot &next) const
{
    if (current != nullptr && !dirtySet.structure) {
        next.units = current->units;
        next.unitIndex = current->unitIndex;
        if (next.unitIndex == nullptr) {
            return;
        }
        for (const auto &unitID : dirtySet.unitIDs) {
            const auto source = view.fragment().find(unitID);
            const auto target = next.unitIndex->find(unitID);
            if (source == view.fragment().end() || target == next.unitIndex->end()) {
                continue;
            }
            next.units[target->second] = std::make_shared<const ResourceUnit>(source->second);
            next.changedUnitIndices.emplace_back(target->second);
        }
        return;
    }

    next.units.reserve(view.fragment_size());
    auto unitIndex = std::make_shared<UnitIndex>();
    unitIndex->reserve(view.fragment_size());
    std::unordered_set<std::string> inserted;
    if (current != nullptr) {
        for (const auto &oldUnit : current->units) {
            const auto source = view.fragment().find(oldUnit->id());
            if (source == view.fragment().end()) {
                continue;
            }
            const auto index = next.units.size();
            if (dirtySet.unitIDs.find(oldUnit->id()) == dirtySet.unitIDs.end()) {
                next.units.emplace_back(oldUnit);
            } else {
                next.units.emplace_back(std::make_shared<const ResourceUnit>(source->second));
            }
            unitIndex->emplace(oldUnit->id(), index);
            inserted.emplace(oldUnit->id());
        }
    }

    for (const auto &[unitID, unit] : view.fragment()) {
        if (inserted.find(unitID) != inserted.end()) {
            continue;
        }
        unitIndex->emplace(unitID, next.units.size());
        next.units.emplace_back(std::make_shared<const ResourceUnit>(unit));
    }
    next.unitIndex = std::move(unitIndex);
}

ScheduleSnapshotPtr ScheduleSnapshotBuilder::BuildAndPublish(
    const ResourceUnit &view, SCHEDULER_LEVEL schedulerLevel,
    const RequestPlacementIndex &requestPlacements, const OwnerLabelIndex &ownerLabels,
    const ScheduleSnapshotDirtySet &dirtySet)
{
    const bool diagnosticsEnabled = store_->DiagnosticsEnabled();
    const auto started = diagnosticsEnabled ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
    auto current = store_->Load();
    auto next = std::make_shared<ScheduleSnapshot>();
    next->revision = view.revision();
    next->publicationSequence = current == nullptr ? 1 : current->publicationSequence + 1;
    next->parentPublicationSequence = current == nullptr ? 0 : current->publicationSequence;
    next->requestMutationSequence = requestMutationJournal_->LatestSequence();
    next->requestMutationJournal = requestMutationJournal_;
    next->viewInitTime = view.viewinittime();
    next->schedulerLevel = schedulerLevel;
    RebuildUnits(view, current, dirtySet, *next);

    if (current != nullptr && !dirtySet.requestPlacements) {
        next->requestPlacements = current->requestPlacements;
    } else {
        next->requestPlacements = std::make_shared<const RequestPlacementIndex>(requestPlacements);
    }
    if (current != nullptr && !dirtySet.ownerLabels) {
        next->ownerLabels = current->ownerLabels;
    } else {
        next->ownerLabels = std::make_shared<const OwnerLabelIndex>(ownerLabels);
    }
    if (current != nullptr && !dirtySet.monopolyIndex && !dirtySet.structure) {
        next->monopolyIndex = current->monopolyIndex;
    } else {
        next->monopolyIndex = UpdateMonopolyIndex(view, current, dirtySet);
    }
    if (current != nullptr && !dirtySet.metadata) {
        next->rootSummary = current->rootSummary;
    } else {
        next->rootSummary = BuildRootSummary(view);
    }

    ScheduleSnapshotPtr ready(std::move(next));
    store_->Publish(ready);
    if (diagnosticsEnabled) {
        store_->RecordBuildDuration(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count()));
    }
    return ready;
}

}  // namespace functionsystem::resource_view
