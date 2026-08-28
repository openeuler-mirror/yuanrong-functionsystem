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

#include "common/schedule_plugin/common/round_allocation_context.h"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "common/resource_view/resource_type.h"

namespace functionsystem::test {

using namespace functionsystem::resource_view;
using namespace functionsystem::schedule_framework;

namespace {

ResourceUnit BuildUnit()
{
    ResourceUnit unit;
    unit.set_id("effective-cache-unit");
    auto &cpu = (*unit.mutable_allocatable()->mutable_resources())[CPU_RESOURCE_NAME];
    cpu.set_name(CPU_RESOURCE_NAME);
    cpu.set_type(ValueType::Value_Type_SCALAR);
    cpu.mutable_scalar()->set_value(1000);
    cpu.mutable_scalar()->set_limit(1000);
    *unit.mutable_capacity() = unit.allocatable();
    return unit;
}

Resources BuildReservedCpu(double value)
{
    Resources reserved;
    auto &cpu = (*reserved.mutable_resources())[CPU_RESOURCE_NAME];
    cpu.set_name(CPU_RESOURCE_NAME);
    cpu.set_type(ValueType::Value_Type_SCALAR);
    cpu.mutable_scalar()->set_value(value);
    cpu.mutable_scalar()->set_limit(value);
    return reserved;
}

std::shared_ptr<ScheduleSnapshot> BuildSnapshot(uint64_t revision, const ResourceUnit &unit,
                                                const RequestPlacementIndex &placements = {})
{
    auto snapshot = std::make_shared<ScheduleSnapshot>();
    snapshot->revision = revision;
    snapshot->viewInitTime = "round-init";
    snapshot->schedulerLevel = SCHEDULER_LEVEL::ROOT_DOMAIN;
    auto unitIndex = std::make_shared<UnitIndex>();
    (*unitIndex)[unit.id()] = 0;
    snapshot->unitIndex = std::move(unitIndex);
    snapshot->units.emplace_back(std::make_shared<const ResourceUnit>(unit));
    snapshot->requestPlacements = std::make_shared<const RequestPlacementIndex>(placements);
    snapshot->ownerLabels = std::make_shared<const OwnerLabelIndex>();
    return snapshot;
}

}  // namespace

TEST(PreAllocatedContextTest, ReusesEffectiveAllocatableOnlyWithinOneUnitEvaluation)
{
    PreAllocatedContext context;
    auto unit = BuildUnit();

    context.BeginUnitEvaluation(unit);
    const auto &first = context.EffectiveAllocatable(unit);
    const auto &second = context.EffectiveAllocatable(unit);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(context.EffectiveAllocatableBuildCount(), 1);
    EXPECT_DOUBLE_EQ(first.resources().at(CPU_RESOURCE_NAME).scalar().value(), 1000);
    context.EndUnitEvaluation();

    context.allocated[unit.id()].resource = BuildReservedCpu(250);
    const auto &afterAllocate = context.EffectiveAllocatable(unit);
    EXPECT_EQ(context.EffectiveAllocatableBuildCount(), 2);
    EXPECT_DOUBLE_EQ(afterAllocate.resources().at(CPU_RESOURCE_NAME).scalar().value(), 750);

    context.allocated[unit.id()].resource = BuildReservedCpu(100);
    const auto &afterRollback = context.EffectiveAllocatable(unit);
    EXPECT_EQ(context.EffectiveAllocatableBuildCount(), 3);
    EXPECT_DOUBLE_EQ(afterRollback.resources().at(CPU_RESOURCE_NAME).scalar().value(), 900);
}

TEST(PreAllocatedContextTest, RoundOverlayIsIdempotentAndConvergesWhenSnapshotConfirmsRequest)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    context.BeginRound(BuildSnapshot(1, unit));

    InstanceInfo instance;
    instance.set_instanceid("instance-1");
    instance.set_requestid("request-1");
    *instance.mutable_resources() = BuildReservedCpu(250);
    instance.mutable_scheduleoption()->set_scheduletimeoutms(18000);
    context.RecordReservation(unit.id(), "request-1", instance);

    ASSERT_NE(context.FindReservationByRequest("request-1"), nullptr);
    EXPECT_EQ(context.ReservationCount(), 1);

    context.BeginRound(BuildSnapshot(2, unit));
    EXPECT_EQ(context.ReservationCount(), 1);
    ASSERT_TRUE(context.allocated.find(unit.id()) != context.allocated.end());
    EXPECT_DOUBLE_EQ(context.allocated.at(unit.id()).resource.resources().at(CPU_RESOURCE_NAME).scalar().value(),
                     250);

    context.BeginRound(BuildSnapshot(3, unit, { { "request-1", unit.id() } }));
    EXPECT_EQ(context.ReservationCount(), 0);
    EXPECT_EQ(context.FindReservationByRequest("request-1"), nullptr);
    EXPECT_TRUE(context.allocated.empty());
}

TEST(PreAllocatedContextTest, RoundOverlayConvergesFromRequestMutationWithoutPlacementScan)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    auto journal = std::make_shared<RequestMutationJournal>();
    auto first = BuildSnapshot(1, unit);
    first->publicationSequence = 1;
    first->requestMutationJournal = journal;
    first->requestMutationSequence = 0;
    context.BeginRound(first);

    InstanceInfo confirmed;
    confirmed.set_instanceid("confirmed-instance");
    confirmed.set_requestid("confirmed-request");
    *confirmed.mutable_resources() = BuildReservedCpu(250);
    context.RecordReservation(unit.id(), confirmed.requestid(), confirmed);

    InstanceInfo pending;
    pending.set_instanceid("pending-instance");
    pending.set_requestid("pending-request");
    *pending.mutable_resources() = BuildReservedCpu(100);
    context.RecordReservation(unit.id(), pending.requestid(), pending);
    ASSERT_EQ(context.ReservationCount(), 2);
    const auto rebuilds = context.OverlayRebuildCount();

    const auto mutationSequence = journal->Append(confirmed.requestid());
    auto second = BuildSnapshot(2, unit);
    second->publicationSequence = 2;
    second->parentPublicationSequence = 1;
    second->unitIndex = first->unitIndex;
    second->requestPlacements = first->requestPlacements;
    second->ownerLabels = first->ownerLabels;
    second->requestMutationJournal = journal;
    second->requestMutationSequence = mutationSequence;
    context.BeginRound(second);

    EXPECT_EQ(context.FindReservationByRequest(confirmed.requestid()), nullptr);
    EXPECT_NE(context.FindReservationByRequest(pending.requestid()), nullptr);
    EXPECT_EQ(context.ReservationCount(), 1);
    EXPECT_EQ(context.ConsumedMutationSequence(), mutationSequence);
    EXPECT_EQ(context.JournalOverflowCount(), 0);
    EXPECT_EQ(context.OverlayRebuildCount(), rebuilds);
    ASSERT_NE(context.allocated.find(unit.id()), context.allocated.end());
    EXPECT_DOUBLE_EQ(context.allocated.at(unit.id()).resource.resources().at(CPU_RESOURCE_NAME).scalar().value(),
                     100);
}

TEST(PreAllocatedContextTest, JournalOverflowFallsBackToPlacementReconciliation)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    auto journal = std::make_shared<RequestMutationJournal>(1);
    auto first = BuildSnapshot(1, unit);
    first->publicationSequence = 1;
    first->requestMutationJournal = journal;
    context.BeginRound(first);

    InstanceInfo instance;
    instance.set_instanceid("instance-1");
    instance.set_requestid("request-1");
    *instance.mutable_resources() = BuildReservedCpu(250);
    context.RecordReservation(unit.id(), instance.requestid(), instance);

    (void)journal->Append("request-0");
    const auto through = journal->Append(instance.requestid());
    auto second = BuildSnapshot(2, unit, { { instance.requestid(), unit.id() } });
    second->publicationSequence = 2;
    second->parentPublicationSequence = 1;
    second->requestMutationJournal = journal;
    second->requestMutationSequence = through;
    context.BeginRound(second);

    EXPECT_EQ(context.JournalOverflowCount(), 1);
    EXPECT_EQ(context.ReservationCount(), 0);
    EXPECT_TRUE(context.allocated.empty());
}

TEST(PreAllocatedContextTest, ReservationJournalReplacesDuplicateRequestAndInstanceKeys)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    context.BeginRound(BuildSnapshot(1, unit));

    InstanceInfo first;
    first.set_instanceid("same-instance");
    *first.mutable_resources() = BuildReservedCpu(100);
    context.RecordReservation(unit.id(), "request-1", first);

    InstanceInfo sameRequest = first;
    sameRequest.set_instanceid("new-instance");
    context.RecordReservation(unit.id(), "request-1", sameRequest);
    ASSERT_EQ(context.ReservationCount(), 1);
    ASSERT_NE(context.FindReservationByRequest("request-1"), nullptr);
    EXPECT_EQ(context.FindReservationByRequest("request-1")->instanceID, "new-instance");

    InstanceInfo sameInstance = sameRequest;
    context.RecordReservation(unit.id(), "request-2", sameInstance);
    EXPECT_EQ(context.ReservationCount(), 1);
    EXPECT_EQ(context.FindReservationByRequest("request-1"), nullptr);
    ASSERT_NE(context.FindReservationByRequest("request-2"), nullptr);
    EXPECT_EQ(context.FindReservationByRequest("request-2")->instanceID, "new-instance");
}

TEST(PreAllocatedContextTest, ResourceOnlyRevisionDoesNotRebuildReservationOverlay)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    auto revision1 = BuildSnapshot(1, unit);
    context.BeginRound(revision1);

    InstanceInfo instance;
    instance.set_instanceid("instance-1");
    instance.set_requestid("request-1");
    *instance.mutable_resources() = BuildReservedCpu(250);
    instance.mutable_scheduleoption()->set_scheduletimeoutms(18000);
    context.RecordReservation(unit.id(), "request-1", instance);
    const auto rebuilds = context.OverlayRebuildCount();

    auto changedUnit = unit;
    changedUnit.mutable_allocatable()
        ->mutable_resources()
        ->at(CPU_RESOURCE_NAME)
        .mutable_scalar()
        ->set_value(900);
    auto revision2 = BuildSnapshot(2, changedUnit);
    revision2->unitIndex = revision1->unitIndex;
    revision2->requestPlacements = revision1->requestPlacements;
    revision2->ownerLabels = revision1->ownerLabels;
    context.BeginRound(revision2);

    EXPECT_EQ(context.OverlayRebuildCount(), rebuilds);
    EXPECT_EQ(context.ReservationCount(), 1);
    EXPECT_DOUBLE_EQ(context.EffectiveAllocatable(*revision2->FindUnit(unit.id()))
                         .resources()
                         .at(CPU_RESOURCE_NAME)
                         .scalar()
                         .value(),
                     650);
}

TEST(PreAllocatedContextTest, PublicationSequenceAppliesIndexChangesWithoutResourceRevisionChange)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    auto first = BuildSnapshot(7, unit);
    first->publicationSequence = 1;
    first->parentPublicationSequence = 0;
    context.BeginRound(first);
    EXPECT_TRUE(context.allLocalLabels.empty());

    auto second = BuildSnapshot(7, unit);
    second->publicationSequence = 2;
    second->parentPublicationSequence = 1;
    second->unitIndex = first->unitIndex;
    second->units = first->units;
    auto labels = std::make_shared<OwnerLabelIndex>();
    (*labels)["local-a"]["zone"].mutable_items()->insert({ "zone-a", 1 });
    second->ownerLabels = labels;
    context.BeginRound(second);

    ASSERT_EQ(context.snapshot, second);
    ASSERT_NE(context.allLocalLabels.find("local-a"), context.allLocalLabels.end());
    EXPECT_EQ(context.allLocalLabels.at("local-a").at("zone").items().at("zone-a"), 1);
}

TEST(PreAllocatedContextTest, RoundOverlayDropsReservationForDeletedUnitWithoutCrossLaneState)
{
    RoundAllocationContext primary;
    RoundAllocationContext virtualLane;
    auto unit = BuildUnit();
    primary.BeginRound(BuildSnapshot(1, unit));
    virtualLane.BeginRound(BuildSnapshot(1, unit));

    InstanceInfo instance;
    instance.set_instanceid("same-instance");
    instance.set_requestid("same-request");
    *instance.mutable_resources() = BuildReservedCpu(100);
    primary.RecordReservation(unit.id(), "same-request", instance);
    EXPECT_EQ(primary.ReservationCount(), 1);
    EXPECT_EQ(virtualLane.ReservationCount(), 0);

    auto empty = std::make_shared<ScheduleSnapshot>();
    empty->revision = 2;
    empty->viewInitTime = "round-init";
    empty->requestPlacements = std::make_shared<const RequestPlacementIndex>();
    empty->ownerLabels = std::make_shared<const OwnerLabelIndex>();
    primary.BeginRound(empty);
    EXPECT_EQ(primary.ReservationCount(), 0);
    EXPECT_EQ(virtualLane.ReservationCount(), 0);
}

TEST(PreAllocatedContextTest, ExpiredReservationIsRemovedFromOverlayWithoutRevisionChange)
{
    RoundAllocationContext context;
    auto unit = BuildUnit();
    auto revision = BuildSnapshot(1, unit);
    context.BeginRound(revision);

    InstanceInfo instance;
    instance.set_instanceid("expiring-instance");
    *instance.mutable_resources() = BuildReservedCpu(100);
    instance.mutable_scheduleoption()->set_scheduletimeoutms(20);
    context.RecordReservation(unit.id(), "expiring-request", instance);
    context.BeginRound(BuildSnapshot(2, unit));
    ASSERT_EQ(context.ReservationCount(), 1);
    ASSERT_FALSE(context.allocated.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto sameRevision = BuildSnapshot(2, unit);
    context.BeginRound(sameRevision);
    EXPECT_EQ(context.ReservationCount(), 0);
    EXPECT_TRUE(context.allocated.empty());
    EXPECT_TRUE(context.allocatedLabels.empty());
}

TEST(PreAllocatedContextTest, DISABLED_ReportReservationAndJournalCost)
{
    constexpr size_t journalHistory = 20000;
    constexpr size_t journalDelta = 64;
    constexpr size_t journalReadIterations = 1000;
    RequestMutationJournal journal(journalHistory);
    for (size_t index = 0; index < journalHistory; ++index) {
        (void)journal.Append("history-request-" + std::to_string(index));
    }

    std::vector<RequestMutation> mutations;
    size_t mutationChecksum = 0;
    const auto journalStart = std::chrono::steady_clock::now();
    for (size_t index = 0; index < journalReadIterations; ++index) {
        EXPECT_TRUE(journal.ReadRange(journalHistory - journalDelta, journalHistory, mutations));
        mutationChecksum += mutations.size();
    }
    const auto journalElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - journalStart);

    constexpr size_t reservationCount = 5000;
    auto unit = BuildUnit();
    RoundAllocationContext context;
    auto snapshot = BuildSnapshot(1, unit);
    snapshot->publicationSequence = 1;
    snapshot->requestMutationJournal = std::make_shared<RequestMutationJournal>(reservationCount * 2);
    context.BeginRound(snapshot);

    const auto reservationStart = std::chrono::steady_clock::now();
    for (size_t index = 0; index < reservationCount; ++index) {
        InstanceInfo instance;
        instance.set_instanceid("reservation-instance-" + std::to_string(index));
        instance.set_requestid("reservation-request-" + std::to_string(index));
        *instance.mutable_resources() = BuildReservedCpu(0.1);
        context.RecordReservation(unit.id(), instance.requestid(), std::move(instance));
    }
    const auto reservationElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - reservationStart);

    for (size_t index = 0; index < reservationCount; ++index) {
        (void)snapshot->requestMutationJournal->Append("reservation-request-" + std::to_string(index));
    }
    auto next = BuildSnapshot(2, unit);
    next->publicationSequence = 2;
    next->parentPublicationSequence = 1;
    next->unitIndex = snapshot->unitIndex;
    next->requestPlacements = snapshot->requestPlacements;
    next->ownerLabels = snapshot->ownerLabels;
    next->requestMutationJournal = snapshot->requestMutationJournal;
    next->requestMutationSequence = snapshot->requestMutationJournal->LatestSequence();
    const auto reconcileStart = std::chrono::steady_clock::now();
    context.BeginRound(next);
    const auto reconcileElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - reconcileStart);

    EXPECT_EQ(mutationChecksum, journalDelta * journalReadIterations);
    EXPECT_EQ(context.ReservationCount(), 0);
    std::cout << "RESERVATION_JOURNAL_COST {\"journal_read_mean_ns\":"
              << journalElapsed.count() / journalReadIterations
              << ",\"reservation_record_mean_ns\":" << reservationElapsed.count() / reservationCount
              << ",\"reservation_reconcile_mean_ns\":" << reconcileElapsed.count() / reservationCount
              << "}" << std::endl;
}

}  // namespace functionsystem::test
