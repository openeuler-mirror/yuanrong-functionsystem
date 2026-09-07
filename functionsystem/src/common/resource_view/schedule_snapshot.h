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

#ifndef COMMON_RESOURCE_VIEW_SCHEDULE_SNAPSHOT_H
#define COMMON_RESOURCE_VIEW_SCHEDULE_SNAPSHOT_H

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "resource_type.h"

namespace functionsystem::resource_view {

using UnitPtr = std::shared_ptr<const ResourceUnit>;
using UnitIndex = std::unordered_map<std::string, size_t>;
using RequestPlacementIndex = std::unordered_map<std::string, std::string>;
using OwnerLabelMap = ::google::protobuf::Map<std::string, ValueCounter>;
using OwnerLabelIndex = std::unordered_map<std::string, OwnerLabelMap>;

// Identifies one request whose placement state changed in ResourceViewActor.
// The sequence is local to the RequestMutationJournal that produced it.
struct RequestMutation {
    uint64_t sequence{ 0 };
    std::string requestID;
};

// Bounded change journal shared by the ResourceViewActor writer and scheduler
// readers. It lets a scheduler reconcile only reservations whose request state
// changed between two snapshot publications instead of scanning the full
// reservation overlay. If the requested range has been overwritten, ReadRange
// returns false and the caller falls back to a full overlay rebuild.
//
// Append and ReadRange may execute on different Actor worker threads. mutex_
// protects the ring slots, firstIndex_, and requestID strings; latestSequence_
// remains atomic so reading the current watermark does not take the lock.
class RequestMutationJournal {
public:
    explicit RequestMutationJournal(size_t capacity = 65536) : capacity_(capacity == 0 ? 1 : capacity)
    {
        mutations_.reserve(capacity_);
    }
    ~RequestMutationJournal() = default;

    uint64_t Append(const std::string &requestID);
    uint64_t LatestSequence() const;
    bool ReadRange(uint64_t afterSequence, uint64_t throughSequence,
                   std::vector<RequestMutation> &mutations) const;

private:
    const size_t capacity_;
    // Protects mutations_ and firstIndex_. It is intentionally not used by
    // LatestSequence or by Filter/Score candidate traversal.
    mutable std::mutex mutex_;
    std::vector<RequestMutation> mutations_;
    size_t firstIndex_{ 0 };
    std::atomic<uint64_t> latestSequence_{ 0 };
};

using RequestMutationJournalPtr = std::shared_ptr<RequestMutationJournal>;

// Precomputed Units that can satisfy one monopoly resource bucket. The index
// avoids repeatedly traversing protobuf bucket maps in the scheduling plugins.
struct MonopolyCandidates {
    int32_t total{ 0 };
    std::vector<std::string> unitIDs;
};

using MonopolyMemoryIndex = std::unordered_map<std::string, MonopolyCandidates>;
using MonopolyPrefilterIndex = std::unordered_map<std::string, MonopolyMemoryIndex>;

// Complete read-only scheduling view published by ScheduleSnapshotStore.
// ScheduleSnapshotBuilder creates a new object for every publication and shares
// unchanged const Units and indexes with its parent publication. After Publish,
// no field of this object or its const children may be modified.
//
// requestMutationJournal is the sole synchronized mutable collaborator: the
// snapshot captures its requestMutationSequence watermark, while readers use
// the journal's thread-safe API to consume changes up to that watermark.
struct ScheduleSnapshot {
    uint64_t revision{ 0 };
    // Local sequence of immutable publications. changedUnitIndices is valid
    // relative to the direct parent publication only.
    uint64_t publicationSequence{ 0 };
    uint64_t parentPublicationSequence{ 0 };
    uint64_t requestMutationSequence{ 0 };
    std::string viewInitTime;
    SCHEDULER_LEVEL schedulerLevel{ SCHEDULER_LEVEL::LOCAL };
    std::vector<UnitPtr> units;
    std::vector<size_t> changedUnitIndices;
    std::shared_ptr<const UnitIndex> unitIndex;
    std::shared_ptr<const RequestPlacementIndex> requestPlacements;
    std::shared_ptr<const OwnerLabelIndex> ownerLabels;
    std::shared_ptr<const MonopolyPrefilterIndex> monopolyIndex;
    std::shared_ptr<const ResourceUnit> rootSummary;
    RequestMutationJournalPtr requestMutationJournal;

    const ResourceUnit *FindUnit(const std::string &unitID) const;
    const MonopolyCandidates *FindMonopolyCandidates(const std::string &proportion,
                                                      const std::string &memory) const;
    ResourceUnit MaterializeResourceView() const;
};

using ScheduleSnapshotPtr = std::shared_ptr<const ScheduleSnapshot>;

// Optional counters for measuring snapshot construction cost. They are kept
// outside ScheduleSnapshot so enabling diagnostics does not change its content.
struct SnapshotBuildDiagnostics {
    uint64_t calls{ 0 };
    uint64_t totalNanos{ 0 };
    uint64_t maxNanos{ 0 };
};

// Read adapter used by scheduling framework and plugins during migration. It
// exposes one API over either the legacy ResourceViewInfo or an immutable
// ScheduleSnapshot. The legacy pointer is non-owning and must not outlive the
// caller; snapshot_ retains the immutable view with shared_ptr.
class ScheduleResourceView {
public:
    explicit ScheduleResourceView(const ResourceViewInfo &legacy);
    explicit ScheduleResourceView(ScheduleSnapshotPtr snapshot);
    ~ScheduleResourceView() = default;

    bool IsSnapshot() const;
    SCHEDULER_LEVEL GetSchedulerLevel() const;
    const RequestPlacementIndex &GetRequestPlacements() const;
    const OwnerLabelIndex &GetOwnerLabels() const;
    const ResourceUnit *FindUnit(const std::string &unitID) const;
    const ResourceUnit *GetLegacyResourceUnit() const;
    const ScheduleSnapshotPtr &GetSnapshot() const;
    ResourceUnit MaterializeResourceView() const;

private:
    const ResourceViewInfo *legacy_{ nullptr };
    ScheduleSnapshotPtr snapshot_;
};

// Thread-safe publication point for the latest immutable snapshot. The
// ResourceViewActor publishes a fully built shared_ptr with release semantics;
// scheduler Actors load it directly with acquire semantics without sending a
// ResourceViewActor mailbox message. Snapshot contents are never mutated after
// publication, so the shared_ptr swap is the only synchronization required.
class ScheduleSnapshotStore {
public:
    ~ScheduleSnapshotStore() = default;

    ScheduleSnapshotPtr Load() const;
    void Publish(ScheduleSnapshotPtr next);
    void EnableDiagnostics(bool enabled);
    bool DiagnosticsEnabled() const;
    void RecordBuildDuration(uint64_t nanos);
    SnapshotBuildDiagnostics GetBuildDiagnostics() const;

private:
    ScheduleSnapshotPtr published_;
    std::atomic<bool> diagnosticsEnabled_{ false };
    std::atomic<uint64_t> buildCalls_{ 0 };
    std::atomic<uint64_t> buildTotalNanos_{ 0 };
    std::atomic<uint64_t> buildMaxNanos_{ 0 };
};

// Accumulates which parts of the next snapshot differ from the current one.
// ResourceViewActor owns and mutates this value on its Actor thread; it is not a
// cross-thread synchronization object. Builder uses it to copy dirty Units and
// indexes while reusing unchanged immutable objects.
struct ScheduleSnapshotDirtySet {
    std::unordered_set<std::string> unitIDs;
    bool structure{ false };
    bool requestPlacements{ false };
    bool ownerLabels{ false };
    bool monopolyIndex{ false };
    bool metadata{ false };

    bool Empty() const;
    void Merge(const ScheduleSnapshotDirtySet &other);
    void Clear();
};

// Single-writer COW builder owned by ResourceViewActor. BuildAndPublish reads
// the Actor-owned mutable ResourceUnit view, constructs the next immutable
// ScheduleSnapshot, reuses unchanged children, and publishes it through store_.
// The builder itself is not thread-safe; only its shared mutation journal
// supports concurrent scheduler reads.
class ScheduleSnapshotBuilder {
public:
    explicit ScheduleSnapshotBuilder(std::shared_ptr<ScheduleSnapshotStore> store);
    ~ScheduleSnapshotBuilder() = default;

    ScheduleSnapshotPtr BuildAndPublish(
        const ResourceUnit &view, SCHEDULER_LEVEL schedulerLevel,
        const RequestPlacementIndex &requestPlacements, const OwnerLabelIndex &ownerLabels,
        const ScheduleSnapshotDirtySet &dirtySet);
    uint64_t RecordRequestMutation(const std::string &requestID);

private:
    void RebuildUnits(const ResourceUnit &view, const ScheduleSnapshotPtr &current,
                      const ScheduleSnapshotDirtySet &dirtySet, ScheduleSnapshot &next) const;
    static std::shared_ptr<const MonopolyPrefilterIndex> BuildMonopolyIndex(const ResourceUnit &view);
    static std::shared_ptr<const MonopolyPrefilterIndex> UpdateMonopolyIndex(
        const ResourceUnit &view, const ScheduleSnapshotPtr &current,
        const ScheduleSnapshotDirtySet &dirtySet);
    static std::shared_ptr<const ResourceUnit> BuildRootSummary(const ResourceUnit &view);

    std::shared_ptr<ScheduleSnapshotStore> store_;
    RequestMutationJournalPtr requestMutationJournal_;
};

}  // namespace functionsystem::resource_view

#endif  // COMMON_RESOURCE_VIEW_SCHEDULE_SNAPSHOT_H
