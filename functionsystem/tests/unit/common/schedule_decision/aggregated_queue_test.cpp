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
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>

#include "common/schedule_decision/queue/aggregated_queue.h"

#include "common/schedule_decision/queue/schedule_queue.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"

namespace functionsystem::test {
using namespace schedule_decision;
class AggregatedQueueTest : public ::testing::Test {
public:
    static std::shared_ptr<InstanceItem> CreateInstanceItem(const std::string &reqId, int priority,double cpu, double memory) {
        auto ins = InstanceItem::CreateInstanceItem(reqId, priority);
        auto instanceInfo1 = view_utils::GetInstanceWithResourceAndPriority(priority, cpu, memory);
        *ins->scheduleReq->mutable_instance() = instanceInfo1;
        return ins;
    }
};

TEST_F(AggregatedQueueTest, InvalidEnqueueTest)
{
    auto priorityQueue = std::make_shared<AggregatedQueue>(3,"strictly");
    auto req = std::make_shared<messages::ScheduleRequest>();

    auto res0 = priorityQueue->Enqueue(nullptr).Get();
    EXPECT_EQ(res0.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(res0.GetMessage(), "[queueItem is null]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);


    auto ins1 = std::make_shared<InstanceItem>(req, std::make_shared<litebus::Promise<ScheduleResult>>(), litebus::Future<std::string>());
    auto res1 = priorityQueue->Enqueue(ins1).Get();
    EXPECT_EQ(res1.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res1.GetMessage(), "[get instance requestId failed]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    auto ins2 = InstanceItem::CreateInstanceItem("ins2",4);
    auto res2 = priorityQueue->Enqueue(ins2).Get();
    EXPECT_EQ(res2.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(res2.GetMessage(), "[instance priority is greater than maxPriority]");
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    auto ins3 = CreateInstanceItem("ins3",1,10,10);
    auto res3 = priorityQueue->Enqueue(ins3).Get();
    EXPECT_EQ(res3.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 1);

}

TEST_F(AggregatedQueueTest, StrictEnqueueTest) {
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "strictly");
    auto priorityQueue2 = std::make_shared<AggregatedQueue>(3, "strictly");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    EXPECT_EQ(priorityQueue->aggregatedReqs[1].size(), size_t{3});

    priorityQueue2->Enqueue(ins1);
    priorityQueue2->Enqueue(ins3);
    priorityQueue2->Enqueue(ins2);
    EXPECT_EQ(priorityQueue2->aggregatedReqs[1].size(), size_t{2});

}

TEST_F(AggregatedQueueTest, RelaxEnqueueTest) {
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto priorityQueue2 = std::make_shared<AggregatedQueue>(3, "relaxed");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    EXPECT_EQ(priorityQueue->aggregatedReqs[1].size(), size_t{2});

    priorityQueue2->Enqueue(ins1);
    priorityQueue2->Enqueue(ins3);
    priorityQueue2->Enqueue(ins2);
    EXPECT_EQ(priorityQueue2->aggregatedReqs[1].size(), size_t{2});
}

TEST_F(AggregatedQueueTest, SemanticSignatureUsesOnlyCandidateCalculationInputs)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto same = CreateInstanceItem("other-request", 1, 10, 10);
    same->scheduleReq->mutable_instance()->set_instanceid("other-instance");
    same->scheduleReq->set_traceid("other-trace");
    (*base->scheduleReq->mutable_instance()->mutable_extensions())[RECEIVED_TIMESTAMP] = "100";
    (*same->scheduleReq->mutable_instance()->mutable_extensions())[RECEIVED_TIMESTAMP] = "200";
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(same));

    auto differentMetadata = CreateInstanceItem("metadata", 1, 10, 10);
    auto *metadataInstance = differentMetadata->scheduleReq->mutable_instance();
    metadataInstance->set_function("function-a");
    metadataInstance->set_parentid("parent-a");
    metadataInstance->set_tenantid("tenant-a");
    metadataInstance->mutable_scheduleoption()->set_target(resources::CreateTarget::RESOURCE_GROUP);
    metadataInstance->mutable_scheduleoption()->set_scheduletimeoutms(1234);
    (*metadataInstance->mutable_scheduleoption()->mutable_nodeselector())["zone"] = "zone-a";
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(differentMetadata));

    auto differentCreateOption = CreateInstanceItem("create-option", 1, 10, 10);
    (*differentCreateOption->scheduleReq->mutable_instance()->mutable_createoptions())["concurrency"] = "2";
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(differentCreateOption));

    auto withLabel = CreateInstanceItem("label", 1, 10, 10);
    withLabel->scheduleReq->mutable_instance()->add_labels("rack-a");
    EXPECT_NE(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(withLabel));

    auto withSelector = CreateInstanceItem("selector", 1, 10, 10);
    (*withSelector->scheduleReq->mutable_instance()
          ->mutable_scheduleoption()
          ->mutable_resourceselector())["resource.owner"] = "tenant-a";
    EXPECT_NE(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(withSelector));

    auto ignoredExtension = CreateInstanceItem("ignored-extension", 1, 10, 10);
    (*ignoredExtension->scheduleReq->mutable_instance()->mutable_scheduleoption()->mutable_extension())["trace"] =
        "trace-a";
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(ignoredExtension));

    auto withNuma = CreateInstanceItem("numa", 1, 10, 10);
    auto *numaExtension = withNuma->scheduleReq->mutable_instance()->mutable_scheduleoption()->mutable_extension();
    (*numaExtension)["bind_resource"] = "NUMA";
    (*numaExtension)["bind_strategy"] = "BIND_Pack";
    EXPECT_NE(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(withNuma));

    auto virtualGroup = CreateInstanceItem("virtual", 1, 10, 10);
    virtualGroup->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_rgroupname("virtual-a");
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(virtualGroup));
}

TEST_F(AggregatedQueueTest, ScalarSignatureIgnoresPluginProcessContexts)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto defaultAffinityContext = CreateInstanceItem("default-affinity-context", 1, 10, 10);
    base->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    defaultAffinityContext->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    (*defaultAffinityContext->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
        .mutable_affinityctx()
        ->set_maxscore(200);
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(defaultAffinityContext));

    auto accumulatedAffinityContext = CreateInstanceItem("accumulated-affinity-context", 1, 10, 10);
    accumulatedAffinityContext->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(
        "shared");
    auto *accumulated = (*accumulatedAffinityContext->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
                            .mutable_affinityctx();
    accumulated->set_maxscore(200);
    (*accumulated->mutable_scheduledscore())["unit-0"] = 1;
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(accumulatedAffinityContext));

    auto topDownAffinityContext = CreateInstanceItem("top-down-affinity-context", 1, 10, 10);
    topDownAffinityContext->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    auto *topDown = (*topDownAffinityContext->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
                        .mutable_affinityctx();
    topDown->set_maxscore(200);
    topDown->set_istopdownscheduling(true);
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(topDownAffinityContext));

    auto otherPluginContext = CreateInstanceItem("other-plugin-context", 1, 10, 10);
    otherPluginContext->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    (*otherPluginContext->scheduleReq->mutable_contexts())[DEFAULT_FILTER_PLUGIN].mutable_defaultctx();
    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(otherPluginContext));
}

TEST_F(AggregatedQueueTest, AffinitySignatureIgnoresCachesButRetainsCrossLevelMode)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto cached = CreateInstanceItem("cached", 1, 10, 10);
    auto topDown = CreateInstanceItem("top-down", 1, 10, 10);
    for (const auto &item : {base, cached, topDown}) {
        item->scheduleReq->mutable_instance()->mutable_scheduleoption()->mutable_affinity()->mutable_instance();
        (*item->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
            .mutable_affinityctx()
            ->set_maxscore(200);
    }
    auto *cachedContext = (*cached->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
    (*cachedContext->mutable_scheduledscore())["unit-a"] = 100;
    (*cachedContext->mutable_scheduledresult())["unit-b"] =
        static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED);
    (*cached->scheduleReq->mutable_contexts())[DEFAULT_FILTER_PLUGIN]
        .mutable_defaultctx()
        ->mutable_filterctx()
        ->insert({ "unit-a", static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) });
    (*topDown->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
        .mutable_affinityctx()
        ->set_istopdownscheduling(true);

    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(cached));
    EXPECT_NE(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(topDown));
}

TEST_F(AggregatedQueueTest, ScalarFastSignatureIncludesCustomScalarResources)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto same = CreateInstanceItem("same", 1, 10, 10);
    auto differentValue = CreateInstanceItem("different-value", 1, 10, 10);
    auto differentName = CreateInstanceItem("different-name", 1, 10, 10);
    for (const auto &item : {base, same, differentValue, differentName}) {
        item->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    }
    auto addScalar = [](const std::shared_ptr<InstanceItem> &item, const std::string &name, double value) {
        auto &resource = (*item->scheduleReq->mutable_instance()->mutable_resources()->mutable_resources())[name];
        resource.set_name(name);
        resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
        resource.mutable_scalar()->set_value(value);
        resource.mutable_scalar()->set_limit(value);
    };
    addScalar(base, "license", 2);
    addScalar(base, "fpga-slot", 1);
    // Insert the same resources in another order; protobuf map iteration order
    // must not fragment an otherwise identical aggregate.
    addScalar(same, "fpga-slot", 1);
    addScalar(same, "license", 2);
    addScalar(differentValue, "license", 3);
    addScalar(differentValue, "fpga-slot", 1);
    addScalar(differentName, "license", 2);
    addScalar(differentName, "asic-slot", 1);

    const auto baseKey = queue->GenerateAggregatedKey(base);
    EXPECT_EQ(baseKey.rfind("scalar-fast", 0), 0u);
    EXPECT_EQ(baseKey, queue->GenerateAggregatedKey(same));
    EXPECT_NE(baseKey, queue->GenerateAggregatedKey(differentValue));
    EXPECT_NE(baseKey, queue->GenerateAggregatedKey(differentName));
}

TEST_F(AggregatedQueueTest, ScalarFastSignatureSupportsStaticResourcePlacementInputs)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto same = CreateInstanceItem("same", 1, 10, 10);
    auto differentAffinity = CreateInstanceItem("different-affinity", 1, 10, 10);
    auto differentSelector = CreateInstanceItem("different-selector", 1, 10, 10);
    auto differentContext = CreateInstanceItem("different-context", 1, 10, 10);
    auto cachedContext = CreateInstanceItem("cached-context", 1, 10, 10);
    for (const auto &item : {base, same, differentAffinity, differentSelector, differentContext, cachedContext}) {
        auto *option = item->scheduleReq->mutable_instance()->mutable_scheduleoption();
        option->set_schedpolicyname("shared");
        option->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(
            Selector(false, { { Exist("accelerator-pool") } }));
        (*option->mutable_resourceselector())["resource.owner"] = "tenant-a";
    }
    differentAffinity->scheduleReq->mutable_instance()
        ->mutable_scheduleoption()
        ->mutable_affinity()
        ->mutable_resource()
        ->mutable_requiredaffinity()
        ->CopyFrom(Selector(false, { { Exist("latency-pool") } }));
    (*differentSelector->scheduleReq->mutable_instance()
          ->mutable_scheduleoption()
          ->mutable_resourceselector())["resource.owner"] = "tenant-b";
    (*differentContext->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
        .mutable_affinityctx()
        ->set_maxscore(200);
    (*cachedContext->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN]
        .mutable_affinityctx()
        ->mutable_scheduledscore()
        ->insert({ "stale-unit", 100 });

    EXPECT_TRUE(IsScalarAggregationEligible(*base->scheduleReq));
    const auto baseKey = queue->GenerateAggregatedKey(base);
    EXPECT_EQ(baseKey.rfind("scalar-fast", 0), 0u);
    EXPECT_EQ(baseKey, queue->GenerateAggregatedKey(same));
    EXPECT_EQ(baseKey, queue->GenerateAggregatedKey(cachedContext));
    EXPECT_NE(baseKey, queue->GenerateAggregatedKey(differentAffinity));
    EXPECT_NE(baseKey, queue->GenerateAggregatedKey(differentSelector));
    EXPECT_NE(baseKey, queue->GenerateAggregatedKey(differentContext));

    auto instanceAffinity = CreateInstanceItem("instance-affinity", 1, 10, 10);
    instanceAffinity->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    instanceAffinity->scheduleReq->mutable_instance()
        ->mutable_scheduleoption()
        ->mutable_affinity()
        ->mutable_instance()
        ->mutable_preferredantiaffinity()
        ->CopyFrom(Selector(false, { { Exist("same-service") } }));
    EXPECT_FALSE(IsScalarAggregationEligible(*instanceAffinity->scheduleReq));

    auto innerAffinity = CreateInstanceItem("inner-affinity", 1, 10, 10);
    innerAffinity->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    innerAffinity->scheduleReq->mutable_instance()
        ->mutable_scheduleoption()
        ->mutable_affinity()
        ->mutable_inner()
        ->mutable_tenant()
        ->mutable_preferredaffinity()
        ->CopyFrom(Selector(false, { { Exist("tenant") } }));
    EXPECT_FALSE(IsScalarAggregationEligible(*innerAffinity->scheduleReq));
}

TEST_F(AggregatedQueueTest, DISABLED_ReportScalarSignatureFastPathAB)
{
    constexpr size_t iterations = 200000;
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed", true);
    auto fast = CreateInstanceItem("fast", 1, 10, 10);
    fast->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    (*fast->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->set_maxscore(200);

    auto canonical = CreateInstanceItem("canonical", 1, 10, 10);
    canonical->scheduleReq->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname("shared");
    auto *canonicalContext =
        (*canonical->scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
    canonicalContext->set_maxscore(200);
    (*canonicalContext->mutable_scheduledscore())["unit-0"] = 1;

    const auto measure = [&](const std::shared_ptr<InstanceItem> &item) {
        size_t generatedBytes = 0;
        const auto started = std::chrono::steady_clock::now();
        for (size_t index = 0; index < iterations; ++index) {
            generatedBytes += queue->GenerateAggregatedKey(item).size();
        }
        const auto elapsed =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
        EXPECT_GT(generatedBytes, 0u);
        return elapsed / iterations;
    };

    const auto fastUs = measure(fast);
    const auto canonicalUs = measure(canonical);
    std::cout << "AGGREGATION_KEY_BENCH {\"iterations\":" << iterations << ",\"fast_us\":" << fastUs
              << ",\"canonical_us\":" << canonicalUs << ",\"speedup\":" << canonicalUs / fastUs << "}"
              << std::endl;
    EXPECT_LT(fastUs, canonicalUs);
}

TEST_F(AggregatedQueueTest, LegacySignatureKeepsCpuMemoryGrouping)
{
    auto queue = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto base = CreateInstanceItem("base", 1, 10, 10);
    auto withDifferentAffinity = CreateInstanceItem("affinity", 1, 10, 10);
    withDifferentAffinity->scheduleReq->mutable_instance()->add_labels("rack-a");

    EXPECT_EQ(queue->GenerateAggregatedKey(base), queue->GenerateAggregatedKey(withDifferentAffinity));
}



TEST_F(AggregatedQueueTest, FrontAndDequeueTest)
{
    auto priorityQueue = std::make_shared<AggregatedQueue>(3, "relaxed");

    auto res = priorityQueue->Dequeue().Get();
    EXPECT_EQ(res.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(res.GetMessage(), "[queue is empty]");

    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    priorityQueue->Enqueue(ins1);
    priorityQueue->Enqueue(ins2);
    priorityQueue->Enqueue(ins3);
    YRLOG_DEBUG("queue size:{}",priorityQueue->queueSize_);
    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins1");
    auto queueItem = priorityQueue->Front();
    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    auto result = priorityQueue->Dequeue().Get();
    EXPECT_EQ(result.StatusCode(), StatusCode::FAILED);
    EXPECT_EQ(result.GetMessage(), "[aggregateItem.reqQueue is not empty]");
    EXPECT_EQ(priorityQueue->queueSize_, 2);

    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins3");
    queueItem = priorityQueue->Front();
    aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 1);

    EXPECT_EQ(priorityQueue->Front()->GetPriority(), 1);
    EXPECT_EQ(priorityQueue->Front()->GetRequestId(), "ins2");
    queueItem = priorityQueue->Front();
    aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(queueItem);
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(priorityQueue->queueSize_, 0);

    EXPECT_EQ(priorityQueue->Front(), nullptr);
    EXPECT_EQ(priorityQueue->Dequeue().Get().StatusCode(), StatusCode::FAILED);

}

TEST_F(AggregatedQueueTest, QueueSwapTest)
{
    auto runningQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    runningQueue_->Enqueue(ins1);
    runningQueue_->Enqueue(ins2);
    runningQueue_->Enqueue(ins3);

    auto ins4 = CreateInstanceItem("ins4", 1, 10, 10);
    pendingQueue_->Enqueue(ins4);
    runningQueue_->Swap(pendingQueue_);


    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(pendingQueue_->Front());
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins1");
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins3");
    aggregatedItem->reqQueue->pop_front();
    pendingQueue_->Dequeue();
    EXPECT_EQ(pendingQueue_->Front()->GetRequestId(), "ins2");
    pendingQueue_->Dequeue();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins4");
}

TEST_F(AggregatedQueueTest, QueueExtendTest)
{
    auto runningQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(3, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 1, 10, 10);
    auto ins2 = CreateInstanceItem("ins2", 1, 15, 20);
    auto ins3 = CreateInstanceItem("ins3", 1, 10, 10);
    runningQueue_->Enqueue(ins1);
    runningQueue_->Enqueue(ins2);
    pendingQueue_->Enqueue(ins3);

    runningQueue_->Extend(pendingQueue_);

    auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(runningQueue_->Front());
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins1");
    aggregatedItem->reqQueue->pop_front();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins3");
    aggregatedItem->reqQueue->pop_front();
    runningQueue_->Dequeue();
    EXPECT_EQ(runningQueue_->Front()->GetRequestId(), "ins2");
    runningQueue_->Dequeue();
}

TEST_F(AggregatedQueueTest, AbnormalTest)
{
    // for LLT lcov
    auto runningQueue_ = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto pendingQueue_ = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto ins1 = CreateInstanceItem("ins1", 3, 10, 10);
    runningQueue_->Enqueue(ins1);
    auto result = runningQueue_->Dequeue();
    EXPECT_EQ(result.Get().StatusCode(), StatusCode::FAILED);

    runningQueue_->Extend(nullptr);

    auto runningQueue1 = std::make_shared<AggregatedQueue>(10, "relaxed");
    auto group1 = GroupItem::CreateGroupItem("group1");
    pendingQueue_->Enqueue(group1);
    EXPECT_EQ(runningQueue1->aggregatedReqs.size(), size_t{0});
    runningQueue1->Extend(pendingQueue_);
    EXPECT_EQ(runningQueue1->aggregatedReqs.size(), size_t{1});

    auto group2 = GroupItem::CreateGroupItem("group2");
    EXPECT_EQ(group2->GetItemType(), QueueItemType::GROUP);
    EXPECT_EQ(group2->GetRequestId(), "group2");
    EXPECT_EQ(group2->GetPriority(), 0);


    auto scheRunningQueue = std::make_shared<ScheduleQueue>(10);
    auto schePendingQueue = std::make_shared<ScheduleQueue>(10);
    auto ins3 = CreateInstanceItem("ins3", 3, 10, 10);
    schePendingQueue->Enqueue(ins3);
    scheRunningQueue->Extend(schePendingQueue);
    EXPECT_EQ(scheRunningQueue->queueMap_.size(), size_t{1});

}





}
