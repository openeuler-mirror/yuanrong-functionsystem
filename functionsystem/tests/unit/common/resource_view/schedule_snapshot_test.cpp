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

#include "common/resource_view/schedule_snapshot.h"

#include <gtest/gtest.h>
#include <google/protobuf/util/message_differencer.h>

#include <chrono>
#include <iostream>

#include "common/constants/constants.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_view.h"
#include "utils/future_test_helper.h"
#include "view_utils.h"

namespace functionsystem::test {

using namespace functionsystem::resource_view;
using namespace functionsystem::test::view_utils;

namespace {

ResourceUnit BuildView(uint64_t revision, std::initializer_list<ResourceUnit> units)
{
    ResourceUnit view;
    view.set_id("snapshot-view");
    view.set_revision(revision);
    view.set_viewinittime("init-time");
    for (const auto &unit : units) {
        (*view.mutable_fragment())[unit.id()] = unit;
    }
    return view;
}

ScheduleSnapshotDirtySet FullDirtySet()
{
    ScheduleSnapshotDirtySet dirty;
    dirty.structure = true;
    dirty.requestPlacements = true;
    dirty.ownerLabels = true;
    dirty.monopolyIndex = true;
    dirty.metadata = true;
    return dirty;
}

}  // namespace

TEST(ScheduleSnapshotTest, ReusesUnchangedUnitsAndKeepsPublishedRevisionImmutable)
{
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    RequestPlacementIndex placements;
    OwnerLabelIndex labels;

    auto unitA = Get1DResourceUnit("unit-a");
    auto unitB = Get1DResourceUnit("unit-b");
    auto view = BuildView(1, { unitA, unitB });
    auto revision1 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, placements, labels, FullDirtySet());

    ASSERT_NE(revision1, nullptr);
    EXPECT_EQ(revision1->publicationSequence, 1);
    EXPECT_EQ(revision1->parentPublicationSequence, 0);
    ASSERT_NE(revision1->FindUnit("unit-a"), nullptr);
    ASSERT_NE(revision1->FindUnit("unit-b"), nullptr);
    const auto unitARevision1 = revision1->units.at(revision1->unitIndex->at("unit-a"));
    const auto unitBRevision1 = revision1->units.at(revision1->unitIndex->at("unit-b"));
    const auto oldCpu = unitARevision1->allocatable().resources().at(CPU_RESOURCE_NAME).scalar().value();

    auto &updatedA = (*view.mutable_fragment())["unit-a"];
    (*updatedA.mutable_allocatable()->mutable_resources())[CPU_RESOURCE_NAME].mutable_scalar()->set_value(oldCpu / 2);
    view.set_revision(2);
    ScheduleSnapshotDirtySet unitDirty;
    unitDirty.unitIDs.emplace("unit-a");
    unitDirty.metadata = true;
    auto revision2 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, placements, labels, unitDirty);

    ASSERT_NE(revision2, nullptr);
    EXPECT_EQ(revision2->publicationSequence, 2);
    EXPECT_EQ(revision2->parentPublicationSequence, 1);
    ASSERT_EQ(revision2->changedUnitIndices.size(), 1);
    EXPECT_EQ(revision2->changedUnitIndices.front(), revision2->unitIndex->at("unit-a"));
    EXPECT_EQ(store->Load(), revision2);
    EXPECT_EQ(revision1->revision, 1);
    EXPECT_DOUBLE_EQ(revision1->FindUnit("unit-a")
                         ->allocatable()
                         .resources()
                         .at(CPU_RESOURCE_NAME)
                         .scalar()
                         .value(),
                     oldCpu);
    EXPECT_NE(revision2->units.at(revision2->unitIndex->at("unit-a")), unitARevision1);
    EXPECT_EQ(revision2->units.at(revision2->unitIndex->at("unit-b")), unitBRevision1);
    EXPECT_EQ(revision2->unitIndex, revision1->unitIndex);
    EXPECT_EQ(revision2->monopolyIndex, revision1->monopolyIndex);
}

TEST(ScheduleSnapshotTest, RequestMutationJournalReadsBoundedRangesAndDetectsOverflow)
{
    RequestMutationJournal journal(3);
    EXPECT_EQ(journal.Append("request-1"), 1);
    EXPECT_EQ(journal.Append("request-2"), 2);

    std::vector<RequestMutation> mutations;
    ASSERT_TRUE(journal.ReadRange(0, 2, mutations));
    ASSERT_EQ(mutations.size(), 2);
    EXPECT_EQ(mutations.front().requestID, "request-1");
    EXPECT_EQ(mutations.back().requestID, "request-2");

    EXPECT_EQ(journal.Append("request-3"), 3);
    EXPECT_EQ(journal.Append("request-4"), 4);
    EXPECT_FALSE(journal.ReadRange(0, 4, mutations));
    ASSERT_TRUE(journal.ReadRange(1, 4, mutations));
    ASSERT_EQ(mutations.size(), 3);
    EXPECT_EQ(mutations.front().sequence, 2);
    EXPECT_EQ(mutations.back().sequence, 4);
}

TEST(ScheduleSnapshotTest, DISABLED_ReportSingleDirtyUnitPublicationCost)
{
    constexpr size_t unitCount = 1000;
    constexpr size_t publicationCount = 500;
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    ResourceUnit view;
    view.set_id("publication-benchmark-view");
    view.set_viewinittime("publication-benchmark-init");
    for (size_t index = 0; index < unitCount; ++index) {
        const auto unitID = "publication-unit-" + std::to_string(index);
        (*view.mutable_fragment())[unitID] = Get1DResourceUnit(unitID);
    }
    (void)builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, FullDirtySet());

    ScheduleSnapshotDirtySet dirty;
    dirty.unitIDs.emplace("publication-unit-0");
    const auto publicationStart = std::chrono::steady_clock::now();
    for (size_t index = 0; index < publicationCount; ++index) {
        view.set_revision(index + 1);
        view.mutable_fragment()
            ->at("publication-unit-0")
            .mutable_allocatable()
            ->mutable_resources()
            ->at(CPU_RESOURCE_NAME)
            .mutable_scalar()
            ->set_value(1000 - (index % 100));
        (void)builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, dirty);
    }
    const auto publicationElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - publicationStart);
    std::cout << "SNAPSHOT_PUBLICATION_COST {\"single_dirty_mean_ns\":"
              << publicationElapsed.count() / publicationCount << "}" << std::endl;
}

TEST(ScheduleSnapshotTest, BuilderPublishesJournalWatermarkWithoutCopyingJournal)
{
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    auto view = BuildView(1, { Get1DResourceUnit("unit-a") });
    auto first = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, FullDirtySet());
    ASSERT_NE(first->requestMutationJournal, nullptr);
    EXPECT_EQ(first->requestMutationSequence, 0);

    EXPECT_EQ(builder.RecordRequestMutation("request-1"), 1);
    ScheduleSnapshotDirtySet placementDirty;
    placementDirty.requestPlacements = true;
    auto second = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN,
                                          { { "request-1", "unit-a" } }, {}, placementDirty);
    EXPECT_EQ(second->requestMutationJournal, first->requestMutationJournal);
    EXPECT_EQ(second->requestMutationSequence, 1);
}

TEST(ScheduleSnapshotTest, RootSummaryCopiesMetadataWithoutEmbeddingUnits)
{
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    auto unit = Get1DResourceUnit("unit-a");
    GenerateMinimumUnitBucketInfo(unit);
    (*unit.mutable_instances())["root-instance"].set_instanceid("root-instance");
    auto view = BuildView(7, { unit });
    view.set_status(static_cast<uint32_t>(UnitStatus::EVICTING));
    view.set_alias("root-alias");
    view.set_ownerid("root-owner");
    view.set_maxinstancenum(123);
    *view.mutable_capacity() = unit.capacity();
    *view.mutable_allocatable() = unit.allocatable();
    *view.mutable_actualuse() = unit.actualuse();
    (*view.mutable_nodelabels())["zone"].mutable_items()->insert({ "z1", 1 });

    auto snapshot = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, FullDirtySet());
    ASSERT_NE(snapshot->rootSummary, nullptr);
    EXPECT_TRUE(snapshot->rootSummary->fragment().empty());
    EXPECT_TRUE(snapshot->rootSummary->instances().empty());
    EXPECT_TRUE(snapshot->rootSummary->bucketindexs().empty());
    EXPECT_EQ(snapshot->rootSummary->id(), view.id());
    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(snapshot->rootSummary->capacity(),
                                                                   view.capacity()));
    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(snapshot->rootSummary->allocatable(),
                                                                   view.allocatable()));
    EXPECT_EQ(snapshot->rootSummary->nodelabels().size(), view.nodelabels().size());
    EXPECT_EQ(snapshot->rootSummary->status(), view.status());
    EXPECT_EQ(snapshot->rootSummary->alias(), view.alias());
    EXPECT_EQ(snapshot->rootSummary->ownerid(), view.ownerid());
    EXPECT_EQ(snapshot->rootSummary->maxinstancenum(), view.maxinstancenum());

    auto materialized = snapshot->MaterializeResourceView();
    EXPECT_EQ(materialized.fragment_size(), 1);
    EXPECT_NE(materialized.fragment().find("unit-a"), materialized.fragment().end());
    EXPECT_NE(materialized.instances().find("root-instance"), materialized.instances().end());
    ASSERT_EQ(materialized.bucketindexs_size(), 1);
    const auto &bucket = materialized.bucketindexs().begin()->second.buckets().begin()->second;
    EXPECT_EQ(bucket.total().monopolynum(), 1);
    EXPECT_EQ(bucket.allocatable().at("unit-a").monopolynum(), 1);
}

TEST(ScheduleSnapshotTest, RebuildsMonopolyIndexOnlyWhenItsSourceChanges)
{
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    auto unit = Get1DResourceUnit("unit-a");
    GenerateMinimumUnitBucketInfo(unit);
    auto view = BuildView(1, { unit });
    *view.mutable_bucketindexs() = unit.bucketindexs();

    auto revision1 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, FullDirtySet());
    ASSERT_NE(revision1->monopolyIndex, nullptr);

    auto revision2 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, ScheduleSnapshotDirtySet{});
    EXPECT_EQ(revision2->monopolyIndex, revision1->monopolyIndex);

    auto &bucket = view.mutable_bucketindexs()->begin()->second.mutable_buckets()->begin()->second;
    bucket.mutable_total()->set_monopolynum(0);
    bucket.mutable_allocatable()->at("unit-a").set_monopolynum(0);
    ScheduleSnapshotDirtySet monopolyDirty;
    monopolyDirty.monopolyIndex = true;
    monopolyDirty.unitIDs.emplace("unit-a");
    auto revision3 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::ROOT_DOMAIN, {}, {}, monopolyDirty);
    EXPECT_NE(revision3->monopolyIndex, revision2->monopolyIndex);
    const auto *candidate = revision3->FindMonopolyCandidates(
        view.bucketindexs().begin()->first, view.bucketindexs().begin()->second.buckets().begin()->first);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->total, 0);
    EXPECT_TRUE(candidate->unitIDs.empty());
}

TEST(ScheduleSnapshotTest, SupportsDynamicUnitAddAndDelete)
{
    auto store = std::make_shared<ScheduleSnapshotStore>();
    ScheduleSnapshotBuilder builder(store);
    RequestPlacementIndex placements;
    OwnerLabelIndex labels;

    auto unitA = Get1DResourceUnit("unit-a");
    auto unitB = Get1DResourceUnit("unit-b");
    auto view = BuildView(1, { unitA, unitB });
    auto revision1 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::LOCAL, placements, labels, FullDirtySet());
    const auto unitBRevision1 = revision1->units.at(revision1->unitIndex->at("unit-b"));

    view.mutable_fragment()->erase("unit-a");
    auto unitC = Get1DResourceUnit("unit-c");
    (*view.mutable_fragment())[unitC.id()] = unitC;
    view.set_revision(2);
    ScheduleSnapshotDirtySet structureDirty;
    structureDirty.structure = true;
    structureDirty.unitIDs.emplace("unit-a");
    structureDirty.unitIDs.emplace("unit-c");
    structureDirty.metadata = true;
    auto revision2 = builder.BuildAndPublish(view, SCHEDULER_LEVEL::LOCAL, placements, labels, structureDirty);

    EXPECT_EQ(revision2->FindUnit("unit-a"), nullptr);
    EXPECT_NE(revision2->FindUnit("unit-c"), nullptr);
    ASSERT_NE(revision2->FindUnit("unit-b"), nullptr);
    EXPECT_EQ(revision2->units.at(revision2->unitIndex->at("unit-b")), unitBRevision1);
    EXPECT_EQ(revision2->units.size(), 2);
    EXPECT_EQ(revision2->unitIndex->size(), 2);
    EXPECT_NE(revision2->unitIndex, revision1->unitIndex);
}

TEST(ScheduleSnapshotTest, ResourceViewsPublishPrimaryAndVirtualIndependently)
{
    ResourceViewActor::Param enabledParam{
        .isLocal = true,
        .enableTenantAffinity = true,
        .tenantPodReuseTimeWindow = 1,
        .enableScheduleSnapshot = true,
    };
    auto primary = ResourceView::CreateResourceView("dual-snapshot-view", enabledParam);
    auto virtualView = ResourceView::CreateResourceView("dual-snapshot-view", enabledParam, VIRTUAL_TAG);
    ASSERT_NE(primary->GetScheduleSnapshotStore(), virtualView->GetScheduleSnapshotStore());

    auto sameUnit = Get1DResourceUnit("same-unit-id");
    auto primaryAdd = primary->AddResourceUnit(sameUnit);
    auto virtualAdd = virtualView->AddResourceUnit(sameUnit);
    ASSERT_AWAIT_READY(primaryAdd);
    ASSERT_AWAIT_READY(virtualAdd);

    auto primaryRevision1 = primary->GetScheduleSnapshotStore()->Load();
    auto virtualRevision1 = virtualView->GetScheduleSnapshotStore()->Load();
    ASSERT_NE(primaryRevision1, nullptr);
    ASSERT_NE(virtualRevision1, nullptr);
    EXPECT_NE(primaryRevision1->FindUnit("same-unit-id"), nullptr);
    EXPECT_NE(virtualRevision1->FindUnit("same-unit-id"), nullptr);

    auto update = primary->UpdateUnitStatus("same-unit-id", UnitStatus::EVICTING);
    ASSERT_AWAIT_READY(update);
    // A non-structural update may be coalesced for up to the publication
    // delay. The view-copy read is also an explicit snapshot flush barrier.
    ASSERT_AWAIT_READY(primary->GetResourceViewCopy());
    auto primaryRevision2 = primary->GetScheduleSnapshotStore()->Load();
    auto virtualStillRevision1 = virtualView->GetScheduleSnapshotStore()->Load();
    ASSERT_NE(primaryRevision2, nullptr);
    EXPECT_GT(primaryRevision2->revision, primaryRevision1->revision);
    EXPECT_EQ(primaryRevision2->FindUnit("same-unit-id")->status(),
              static_cast<uint32_t>(UnitStatus::EVICTING));
    EXPECT_EQ(virtualStillRevision1, virtualRevision1);
    EXPECT_EQ(virtualStillRevision1->FindUnit("same-unit-id")->status(),
              static_cast<uint32_t>(UnitStatus::NORMAL));
}

TEST(ScheduleSnapshotTest, DisabledPathDoesNotPublishSnapshot)
{
    ResourceViewActor::Param disabledParam{
        .isLocal = true,
        .enableTenantAffinity = true,
        .tenantPodReuseTimeWindow = 1,
        .enableScheduleSnapshot = false,
    };
    auto view = ResourceView::CreateResourceView("disabled-snapshot-view", disabledParam);
    auto add = view->AddResourceUnit(Get1DResourceUnit("unit-a"));
    ASSERT_AWAIT_READY(add);
    ASSERT_NE(view->GetScheduleSnapshotStore(), nullptr);
    EXPECT_EQ(view->GetScheduleSnapshotStore()->Load(), nullptr);
}

}  // namespace functionsystem::test
