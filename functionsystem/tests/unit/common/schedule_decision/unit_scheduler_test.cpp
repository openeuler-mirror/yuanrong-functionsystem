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

#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/schedule_decision/performer/aggregated_schedule_performer.h"
#include "common/schedule_decision/queue/aggregated_queue.h"
#include "common/schedule_decision/scheduler/unit_scheduler.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"

namespace functionsystem::test {
namespace {

using namespace schedule_decision;
using namespace schedule_framework;

class SnapshotCapableFramework : public Framework {
public:
    bool RegisterPolicy(const std::shared_ptr<SchedulePolicyPlugin> &) override
    {
        return true;
    }

    bool UnRegisterPolicy(const std::string &) override
    {
        return true;
    }

    ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &,
                                   const resource_view::InstanceInfo &,
                                   const resource_view::ResourceUnit &, uint32_t) override
    {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG), "legacy", {} };
    }

    bool SupportsScheduleSnapshot() const override
    {
        return true;
    }
};

class CountingReevaluationFramework : public SnapshotCapableFramework {
public:
    ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &,
                                   const resource_view::InstanceInfo &,
                                   const resource_view::ScheduleResourceView &resourceView,
                                   uint32_t) override
    {
        ++globalScanCount;
        std::priority_queue<NodeScore> candidates;
        for (const auto &unit : resourceView.GetSnapshot()->units) {
            NodeScore score(unit->id(), 100);
            score.availableForRequest = 1;
            candidates.emplace(std::move(score));
        }
        return ScheduleResults{ static_cast<int32_t>(StatusCode::SUCCESS), "", std::move(candidates) };
    }

    CandidateEvaluation EvaluateUnit(const std::shared_ptr<ScheduleContext> &,
                                     const resource_view::InstanceInfo &,
                                     const resource_view::ScheduleResourceView &,
                                     const std::string &unitID) override
    {
        ++reevaluationCount;
        auto &selected = selections[unitID];
        ++selected;
        if (selected >= maxPerUnit) {
            return CandidateEvaluation{ Status(StatusCode::RESOURCE_NOT_ENOUGH, "unit exhausted"),
                                        false, false, NodeScore{ 0 } };
        }
        NodeScore score(unitID, 100);
        score.availableForRequest = 1;
        return CandidateEvaluation{ Status::OK(), true, false, std::move(score) };
    }

    int globalScanCount{ 0 };
    int reevaluationCount{ 0 };
    int maxPerUnit{ 2 };
    std::unordered_map<std::string, int> selections;
};

class CountingBuiltInFramework : public FrameworkImpl {
public:
    ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &,
                                   const resource_view::InstanceInfo &,
                                   const resource_view::ScheduleResourceView &resourceView,
                                   uint32_t) override
    {
        ++globalScanCount;
        std::priority_queue<NodeScore> candidates;
        for (const auto &unit : resourceView.GetSnapshot()->units) {
            NodeScore score(unit->id(), 100);
            score.availableForRequest = 100;
            candidates.emplace(std::move(score));
        }
        return ScheduleResults{ static_cast<int32_t>(StatusCode::SUCCESS), "", std::move(candidates) };
    }

    CandidateEvaluation EvaluateUnit(const std::shared_ptr<ScheduleContext> &,
                                     const resource_view::InstanceInfo &,
                                     const resource_view::ScheduleResourceView &,
                                     const std::string &unitID) override
    {
        ++reevaluationCount;
        NodeScore score(unitID, 100);
        score.availableForRequest = 100;
        return CandidateEvaluation{ Status::OK(), true, false, std::move(score) };
    }

    int globalScanCount{ 0 };
    int reevaluationCount{ 0 };
};

resource_view::ScheduleSnapshotPtr BuildSnapshot(
    std::initializer_list<resource_view::ResourceUnit> units)
{
    auto snapshot = std::make_shared<resource_view::ScheduleSnapshot>();
    snapshot->revision = 1;
    snapshot->viewInitTime = "unit-scheduler-test";
    snapshot->schedulerLevel = resource_view::SCHEDULER_LEVEL::ROOT_DOMAIN;
    snapshot->requestPlacements = std::make_shared<const resource_view::RequestPlacementIndex>();
    snapshot->ownerLabels = std::make_shared<const resource_view::OwnerLabelIndex>();
    auto unitIndex = std::make_shared<resource_view::UnitIndex>();
    for (auto unit : units) {
        unit.set_ownerid("owner-" + unit.id());
        unitIndex->emplace(unit.id(), snapshot->units.size());
        snapshot->units.emplace_back(std::make_shared<const resource_view::ResourceUnit>(std::move(unit)));
    }
    snapshot->unitIndex = std::move(unitIndex);
    return snapshot;
}

std::shared_ptr<InstanceItem> BuildItem(const std::string &requestID)
{
    auto request = std::make_shared<messages::ScheduleRequest>();
    auto instance = view_utils::GetInstanceWithResourceAndPriority(
        1, view_utils::INST_SCALA_VALUE, view_utils::INST_SCALA_VALUE);
    instance.set_requestid(requestID);
    instance.set_instanceid("instance-" + requestID);
    *request->mutable_instance() = instance;
    request->set_requestid(requestID);
    request->set_traceid("trace-" + requestID);
    auto promise = std::make_shared<litebus::Promise<schedule_decision::ScheduleResult>>();
    return std::make_shared<InstanceItem>(request, promise, litebus::Future<std::string>());
}

TEST(UnitSchedulerTest, BeginScheduleRoundPinsSnapshot)
{
    auto framework = std::make_shared<SnapshotCapableFramework>();
    auto scheduler = std::make_shared<UnitScheduler>();
    scheduler->RegisterSchedulePerformer(nullptr, framework, nullptr);

    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("same-unit") });
    EXPECT_TRUE(scheduler->BeginScheduleRound(snapshot).IsOk());
    ASSERT_NE(scheduler->roundContext_, nullptr);
    EXPECT_EQ(scheduler->roundContext_->snapshot, snapshot);
}

TEST(UnitSchedulerTest, LocalAllocationUsesMailboxViewBackend)
{
    auto framework = std::make_shared<SnapshotCapableFramework>();
    auto scheduler = std::make_shared<UnitScheduler>();
    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-local-mailbox", resource_view::ResourceViewActor::Param{ true, false, 0, false });
    scheduler->RegisterSchedulePerformer(resourceView, framework, nullptr, AllocateType::ALLOCATION);
    EXPECT_FALSE(scheduler->UsesScheduleSnapshot());
}

TEST(UnitSchedulerTest, LocalAllocationReservationSurvivesPartialSnapshotUntilRequestIsVisible)
{
    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("unit-a") });
    auto context = std::make_shared<RoundAllocationContext>();
    context->BeginRound(snapshot);

    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-local-allocation", resource_view::ResourceViewActor::Param{ true, false, 0, true });
    SchedulePerformer performer(AllocateType::ALLOCATION);
    performer.BindResourceView(resourceView);
    auto item = BuildItem("local-request");
    schedule_decision::ScheduleResult result{ "owner-unit-a", static_cast<int32_t>(StatusCode::SUCCESS), "" };
    result.unitID = "unit-a";
    performer.Allocate(context, "unit-a", item->scheduleReq->requestid(), item->scheduleReq->instance(), result);

    EXPECT_EQ(context->ReservationCount(), 1);
    EXPECT_NE(result.allocatedPromise, nullptr);

    auto partialMutable = std::make_shared<resource_view::ScheduleSnapshot>(*snapshot);
    partialMutable->revision = 2;
    partialMutable->units[0] = std::make_shared<const resource_view::ResourceUnit>(*snapshot->units[0]);
    resource_view::ScheduleSnapshotPtr partial(std::move(partialMutable));
    context->BeginRound(partial);
    EXPECT_EQ(context->ReservationCount(), 1);

    auto confirmedMutable = std::make_shared<resource_view::ScheduleSnapshot>(*partial);
    confirmedMutable->revision = 3;
    auto placements = std::make_shared<resource_view::RequestPlacementIndex>();
    placements->emplace("local-request", "unit-a");
    confirmedMutable->requestPlacements = std::move(placements);
    resource_view::ScheduleSnapshotPtr confirmed(std::move(confirmedMutable));
    context->BeginRound(confirmed);
    EXPECT_EQ(context->ReservationCount(), 0);
}

TEST(UnitSchedulerTest, SnapshotAggregationScansOnceAndRefreshesOnlySelectedUnit)
{
    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("unit-a"),
                                    view_utils::Get1DResourceUnit("unit-b") });
    auto framework = std::make_shared<CountingReevaluationFramework>();
    auto performer = std::make_shared<AggregatedSchedulePerformer>(AllocateType::PRE_ALLOCATION);
    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-test", resource_view::ResourceViewActor::Param{ false, false, 0 });
    performer->BindResourceView(resourceView);
    performer->RegisterScheduleFramework(framework);
    performer->SetPlacementPolicy("spread");

    auto queue = std::make_shared<AggregatedQueue>(10, RELAXED_AGGREGATE_STRATEGY);
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(queue->Enqueue(BuildItem("request-" + std::to_string(i))).Get().IsOk());
    }
    auto aggregate = std::dynamic_pointer_cast<AggregatedItem>(queue->Front());
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->reqQueue->size(), 4);

    auto context = std::make_shared<RoundAllocationContext>();
    context->BeginRound(snapshot);
    auto results = performer->DoSchedule(context, resource_view::ScheduleResourceView(snapshot), aggregate);

    ASSERT_EQ(results->size(), 4);
    EXPECT_EQ(framework->globalScanCount, 1);
    EXPECT_EQ(framework->reevaluationCount, 4);
    EXPECT_EQ(framework->selections["unit-a"], 2);
    EXPECT_EQ(framework->selections["unit-b"], 2);
    for (const auto &result : *results) {
        EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::SUCCESS));
        EXPECT_FALSE(result.unitID.empty());
        EXPECT_EQ(result.id, "owner-" + result.unitID);
    }
}

TEST(UnitSchedulerTest, BuiltInScalarAggregationReusesCandidateCapacityWithoutReevaluation)
{
    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("unit-a"),
                                    view_utils::Get1DResourceUnit("unit-b") });
    auto framework = std::make_shared<CountingBuiltInFramework>();
    auto performer = std::make_shared<AggregatedSchedulePerformer>(AllocateType::PRE_ALLOCATION);
    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-scalar-fast-path", resource_view::ResourceViewActor::Param{ false, false, 0 });
    performer->BindResourceView(resourceView);
    performer->RegisterScheduleFramework(framework);
    performer->SetPlacementPolicy("spread");

    auto queue = std::make_shared<AggregatedQueue>(10, RELAXED_AGGREGATE_STRATEGY);
    for (int i = 0; i < 4; ++i) {
        auto item = BuildItem("scalar-request-" + std::to_string(i));
        item->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
        if (i == 0) {
            auto *affinity = (*item->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
                                 .mutable_affinityctx();
            affinity->set_maxscore(200);
            (*affinity->mutable_scheduledscore())["stale-unit"] = 100;
            (*item->scheduleReq->mutable_contexts())[DEFAULT_FILTER_PLUGIN]
                .mutable_defaultctx()
                ->mutable_filterctx()
                ->insert({ "stale-unit", static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) });
        }
        EXPECT_TRUE(queue->Enqueue(item).Get().IsOk());
    }
    auto aggregate = std::dynamic_pointer_cast<AggregatedItem>(queue->Front());
    ASSERT_NE(aggregate, nullptr);

    auto context = std::make_shared<RoundAllocationContext>();
    context->BeginRound(snapshot);
    auto results = performer->DoSchedule(context, resource_view::ScheduleResourceView(snapshot), aggregate);

    ASSERT_EQ(results->size(), 4);
    EXPECT_EQ(framework->globalScanCount, 1);
    EXPECT_EQ(framework->reevaluationCount, 0);
    const auto &firstContexts = aggregate->reqQueue->front()->scheduleReq->contexts();
    EXPECT_TRUE(firstContexts.at(LABEL_AFFINITY_PLUGIN).affinityctx().scheduledscore().empty());
    EXPECT_EQ(firstContexts.at(LABEL_AFFINITY_PLUGIN).affinityctx().maxscore(), 200);
    EXPECT_TRUE(firstContexts.at(DEFAULT_FILTER_PLUGIN).defaultctx().filterctx().empty());
    for (const auto &result : *results) {
        EXPECT_EQ(result.code, static_cast<int32_t>(StatusCode::SUCCESS));
        EXPECT_FALSE(result.unitID.empty());
    }
}

TEST(UnitSchedulerTest, BuiltInCustomScalarAggregationReusesCandidateCapacityWithoutReevaluation)
{
    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("unit-a"),
                                    view_utils::Get1DResourceUnit("unit-b") });
    auto framework = std::make_shared<CountingBuiltInFramework>();
    auto performer = std::make_shared<AggregatedSchedulePerformer>(AllocateType::PRE_ALLOCATION);
    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-custom-scalar-fast-path", resource_view::ResourceViewActor::Param{ false, false, 0 });
    performer->BindResourceView(resourceView);
    performer->RegisterScheduleFramework(framework);

    auto queue = std::make_shared<AggregatedQueue>(10, RELAXED_AGGREGATE_STRATEGY);
    for (int i = 0; i < 4; ++i) {
        auto item = BuildItem("custom-scalar-request-" + std::to_string(i));
        auto *instance = item->scheduleReq->mutable_instance();
        instance->mutable_scheduleoption()->set_schedpolicyname("shared");
        auto &resource = (*instance->mutable_resources()->mutable_resources())["license"];
        resource.set_name("license");
        resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
        resource.mutable_scalar()->set_value(1);
        resource.mutable_scalar()->set_limit(1);
        EXPECT_TRUE(queue->Enqueue(item).Get().IsOk());
    }
    auto aggregate = std::dynamic_pointer_cast<AggregatedItem>(queue->Front());
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->reqQueue->size(), 4);

    auto context = std::make_shared<RoundAllocationContext>();
    context->BeginRound(snapshot);
    auto results = performer->DoSchedule(context, resource_view::ScheduleResourceView(snapshot), aggregate);

    ASSERT_EQ(results->size(), 4);
    EXPECT_EQ(framework->globalScanCount, 1);
    EXPECT_EQ(framework->reevaluationCount, 0);
}

TEST(UnitSchedulerTest, BuiltInResourceAffinityAggregationReusesCandidateCapacityWithoutReevaluation)
{
    auto snapshot = BuildSnapshot({ view_utils::Get1DResourceUnit("unit-a"),
                                    view_utils::Get1DResourceUnit("unit-b") });
    auto framework = std::make_shared<CountingBuiltInFramework>();
    auto performer = std::make_shared<AggregatedSchedulePerformer>(AllocateType::PRE_ALLOCATION);
    std::shared_ptr<resource_view::ResourceView> resourceView = resource_view::ResourceView::CreateResourceView(
        "unit-scheduler-resource-affinity-fast-path", resource_view::ResourceViewActor::Param{ false, false, 0 });
    performer->BindResourceView(resourceView);
    performer->RegisterScheduleFramework(framework);

    auto queue = std::make_shared<AggregatedQueue>(10, RELAXED_AGGREGATE_STRATEGY);
    for (int i = 0; i < 4; ++i) {
        auto item = BuildItem("resource-affinity-request-" + std::to_string(i));
        auto *option = item->scheduleReq->mutable_instance()->mutable_scheduleoption();
        option->set_schedpolicyname("shared");
        option->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(
            Selector(false, { { Exist("accelerator-pool") } }));
        (*option->mutable_resourceselector())["resource.owner"] = "tenant-a";
        EXPECT_TRUE(queue->Enqueue(item).Get().IsOk());
    }
    auto aggregate = std::dynamic_pointer_cast<AggregatedItem>(queue->Front());
    ASSERT_NE(aggregate, nullptr);
    ASSERT_EQ(aggregate->reqQueue->size(), 4);

    auto context = std::make_shared<RoundAllocationContext>();
    context->BeginRound(snapshot);
    auto results = performer->DoSchedule(context, resource_view::ScheduleResourceView(snapshot), aggregate);

    ASSERT_EQ(results->size(), 4);
    EXPECT_EQ(framework->globalScanCount, 1);
    EXPECT_EQ(framework->reevaluationCount, 0);
}

TEST(UnitSchedulerTest, SemanticAggregationSeparatesAffinityAndPriority)
{
    auto scheduler = std::make_shared<UnitScheduler>(nullptr, 10, PriorityPolicyType::FIFO,
                                                     RELAXED_AGGREGATE_STRATEGY);
    auto base = BuildItem("base");
    auto same = BuildItem("same");
    auto withAffinity = BuildItem("affinity");
    withAffinity->scheduleReq->mutable_instance()->add_labels("rack-a");
    auto withPriority = BuildItem("priority");
    withPriority->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_priority(2);

    ASSERT_TRUE(scheduler->Enqueue(base).Get().IsOk());
    ASSERT_TRUE(scheduler->Enqueue(same).Get().IsOk());
    ASSERT_TRUE(scheduler->Enqueue(withAffinity).Get().IsOk());
    ASSERT_TRUE(scheduler->Enqueue(withPriority).Get().IsOk());

    auto queue = std::dynamic_pointer_cast<AggregatedQueue>(scheduler->runningQueue_);
    ASSERT_NE(queue, nullptr);
    EXPECT_EQ(queue->Size(), 3);
}

TEST(UnitSchedulerTest, ClusterPlacementPolicyOnlyChangesAggregatedPerformer)
{
    auto framework = std::make_shared<SnapshotCapableFramework>();
    auto scheduler = std::make_shared<UnitScheduler>();
    scheduler->RegisterSchedulePerformer(nullptr, framework, nullptr);
    scheduler->SetPlacementPolicy("spread");

    EXPECT_EQ(scheduler->aggregatedPerformer_->placementPolicy_, "spread");
    EXPECT_EQ(scheduler->instancePerformer_->placementPolicy_, "binpack");
    EXPECT_EQ(scheduler->groupPerformer_->placementPolicy_, "binpack");
}

}  // namespace
}  // namespace functionsystem::test
