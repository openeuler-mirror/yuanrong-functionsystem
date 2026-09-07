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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "async/async.hpp"
#include "common/constants/constants.h"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_decision/schedule_queue_actor.h"
#include "common/schedule_decision/scheduler.h"
#include "common/schedule_decision/scheduler/priority_scheduler.h"
#if __has_include("common/schedule_decision/scheduler/unit_scheduler.h")
#define YR_BENCH_HAS_UNIT_SCHEDULER 1
#include "common/schedule_decision/scheduler/unit_scheduler.h"
#else
#define YR_BENCH_HAS_UNIT_SCHEDULER 0
#endif
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_factory.h"
#include "common/schedule_plugin/common/plugin_utils.h"
#include "common/schedule_plugin/filter/default_filter/default_filter.h"
#include "common/schedule_plugin/filter/default_heterogeneous_filter/default_heterogeneous_filter.h"
#include "common/schedule_plugin/filter/disk_filter/disk_filter.h"
#include "common/schedule_plugin/filter/numa_affinity_filter/numa_affinity_filter.h"
#include "common/schedule_plugin/filter/resource_selector_filter/resource_selector_filter.h"
#include "common/schedule_plugin/prefilter/default_prefilter/default_prefilter.h"
#include "common/schedule_plugin/scorer/default_heterogeneous_scorer/default_heterogeneous_scorer.h"
#include "common/schedule_plugin/scorer/default_scorer/default_scorer.h"
#include "common/schedule_plugin/scorer/disk_scorer/disk_scorer.h"
#include "common/schedule_plugin/scorer/numa_affinity_scorer/numa_affinity_scorer.h"
#include "common/scheduler_framework/framework/framework_impl.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"
#include "logs/api/null.h"
#include "logs/api/provider.h"
#include "mocks/mock_resource_view.h"

namespace functionsystem::test {
namespace {

using Clock = std::chrono::steady_clock;
using schedule_decision::PriorityPolicyType;
using schedule_decision::ScheduleQueueActor;
using schedule_decision::ScheduleResult;

constexpr double REQUEST_CPU = 300.0;
constexpr double REQUEST_MEMORY = 128.0;
constexpr int DISK_REQUEST = 100;
constexpr char UPDATE_NOISE_RESOURCE[] = "current_path_benchmark_noise";

namespace test_plugin = functionsystem::test::schedule_plugin;
namespace prod_plugin = functionsystem::schedule_plugin;

enum class Engine { LEGACY, UNIT };
enum class RequestKind { PLAIN, REQUIRED_AFFINITY, WEAK_ANTI_AFFINITY, NPU, STORAGE, NUMA };

std::vector<Engine> BenchmarkEngines()
{
#if YR_BENCH_HAS_UNIT_SCHEDULER
    return { Engine::LEGACY, Engine::UNIT };
#else
    return { Engine::LEGACY };
#endif
}

const char *EngineName(Engine engine)
{
    return engine == Engine::LEGACY ? "legacy" : "unit_snapshot";
}

bool MatchesBenchmarkFilter(const char *name, const char *value)
{
    const auto *filter = std::getenv(name);
    if (filter == nullptr) {
        return true;
    }
    const auto configured = "," + std::string(filter) + ",";
    return configured.find("," + std::string(value) + ",") != std::string::npos;
}

int BenchmarkRequestCount(int defaultValue)
{
    const auto *value = std::getenv("DOMAIN_BENCH_REQUEST_COUNT");
    if (value == nullptr) {
        return defaultValue;
    }
    return std::max(1, std::stoi(value));
}

int BenchmarkInflight(int defaultValue)
{
    const auto *value = std::getenv("DOMAIN_BENCH_INFLIGHT");
    if (value == nullptr) {
        return defaultValue;
    }
    return std::max(1, std::stoi(value));
}

std::string BenchmarkPlacementPolicy()
{
    const auto *value = std::getenv("DOMAIN_BENCH_PLACEMENT_POLICY");
    return value == nullptr ? "binpack" : value;
}

bool BenchmarkEqualFreshness()
{
    const auto *value = std::getenv("DOMAIN_BENCH_EQUAL_FRESHNESS");
    return value == nullptr || std::string(value) != "0";
}

const char *RequestKindName(RequestKind kind)
{
    switch (kind) {
        case RequestKind::PLAIN:
            return "plain";
        case RequestKind::REQUIRED_AFFINITY:
            return "required_affinity";
        case RequestKind::WEAK_ANTI_AFFINITY:
            return "weak_anti_affinity";
        case RequestKind::NPU:
            return "npu";
        case RequestKind::STORAGE:
            return "storage";
        case RequestKind::NUMA:
            return "numa";
    }
    return "unknown";
}

double Percentile(std::vector<double> values, double percentile)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>((percentile / 100.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

bool IsValidPlacement(RequestKind kind, const std::string &unitID)
{
    constexpr char PREFIX[] = "unit-";
    if (unitID.rfind(PREFIX, 0) != 0) {
        return false;
    }
    int index = -1;
    try {
        index = std::stoi(unitID.substr(sizeof(PREFIX) - 1));
    } catch (...) {
        return false;
    }
    if (kind == RequestKind::REQUIRED_AFFINITY) {
        return index % 4 == 0;
    }
    if (kind == RequestKind::NPU) {
        return index % 5 == 0;
    }
    if (kind == RequestKind::STORAGE) {
        return index % 3 == 0;
    }
    return true;
}

void ExpandNpuCards(resource_view::Resource &npu, int cardCount)
{
    for (auto &[categoryName, category] : *npu.mutable_vectors()->mutable_values()) {
        if (category.vectors().empty()) {
            continue;
        }
        auto *values = category.mutable_vectors()->begin()->second.mutable_values();
        for (int card = values->size(); card < cardCount; ++card) {
            if (categoryName == resource_view::HETEROGENEOUS_MEM_KEY ||
                categoryName == resource_view::HETEROGENEOUS_STREAM_KEY) {
                values->Add(100.0);
            } else if (categoryName == resource_view::IDS_KEY) {
                values->Add(static_cast<double>(card + 100));
            } else {
                values->Add(0.0);
            }
        }
    }
}

resource_view::ResourceUnit MakeUnit(int index, int requestCount, bool mixed)
{
    const auto capacityMultiplier = static_cast<double>(std::max(1, requestCount));
    auto unit = test_plugin::GetAgentResourceUnit(
        REQUEST_CPU * capacityMultiplier, REQUEST_MEMORY * capacityMultiplier, requestCount);
    unit.set_id("unit-" + std::to_string(index));
    unit.set_status(static_cast<uint32_t>(resource_view::UnitStatus::NORMAL));
    auto noise = view_utils::GetNameResourceWithValue(UPDATE_NOISE_RESOURCE, 1000000.0);
    (*unit.mutable_capacity()->mutable_resources())[UPDATE_NOISE_RESOURCE] = noise;
    (*unit.mutable_allocatable()->mutable_resources())[UPDATE_NOISE_RESOURCE] = noise;

    const auto npuName = view_utils::DEFAULT_NPU_TYPE;
    if (mixed && index % 5 == 0) {
        auto npu = unit.capacity().resources().at(npuName);
        ExpandNpuCards(npu, std::max(8, requestCount / 10 + 2));
        (*unit.mutable_capacity()->mutable_resources())[npuName] = npu;
        (*unit.mutable_allocatable()->mutable_resources())[npuName] = std::move(npu);
    } else {
        unit.mutable_capacity()->mutable_resources()->erase(npuName);
        unit.mutable_allocatable()->mutable_resources()->erase(npuName);
    }
    if (mixed && index % 3 == 0) {
        auto disk = view_utils::GetDiskResource({ DISK_REQUEST * requestCount }, unit.id());
        (*unit.mutable_capacity()->mutable_resources())[resource_view::DISK_RESOURCE_NAME] = disk;
        (*unit.mutable_allocatable()->mutable_resources())[resource_view::DISK_RESOURCE_NAME] = std::move(disk);
    }
    if (mixed) {
        auto numa = view_utils::GetNUMAResource(
            { REQUEST_CPU * capacityMultiplier / 2, REQUEST_CPU * capacityMultiplier / 2 }, unit.id());
        (*unit.mutable_capacity()->mutable_resources())[resource_view::NUMA_RESOURCE_NAME] = numa;
        (*unit.mutable_allocatable()->mutable_resources())[resource_view::NUMA_RESOURCE_NAME] = std::move(numa);
    }
    if (mixed && index % 4 == 0) {
        (*unit.mutable_nodelabels())["affinity_pool"] = test_plugin::GetCounter("enabled", 1);
    }
    return unit;
}

std::shared_ptr<messages::ScheduleRequest> MakeRequest(int index, RequestKind kind)
{
    auto request = std::make_shared<messages::ScheduleRequest>();
    auto instance = view_utils::GetInstanceWithResourceAndPriority(0, REQUEST_CPU, REQUEST_MEMORY);
    instance.set_instanceid("benchmark-instance-" + std::to_string(index));
    instance.set_requestid("benchmark-request-" + std::to_string(index));
    instance.mutable_scheduleoption()->set_schedpolicyname("shared");

    if (kind == RequestKind::REQUIRED_AFFINITY) {
        auto *affinity = instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource();
        affinity->mutable_requiredaffinity()->CopyFrom(Selector(false, { { Exist("affinity_pool") } }));
    } else if (kind == RequestKind::WEAK_ANTI_AFFINITY) {
        auto *anti = instance.mutable_scheduleoption()->mutable_affinity()->mutable_instance()
                         ->mutable_preferredantiaffinity();
        anti->CopyFrom(Selector(false, { { Exist("benchmark_batch") } }));
        instance.add_labels("benchmark_batch:same");
    } else if (kind == RequestKind::NPU) {
        const auto templateInstance = view_utils::Get1DInstanceWithNpuResource(1);
        const auto name = view_utils::DEFAULT_NPU_TYPE + "/" + resource_view::HETEROGENEOUS_CARDNUM_KEY;
        (*instance.mutable_resources()->mutable_resources())[name] =
            templateInstance.resources().resources().at(name);
    } else if (kind == RequestKind::STORAGE) {
        (*instance.mutable_resources()->mutable_resources())[resource_view::DISK_RESOURCE_NAME] =
            view_utils::GetNameResourceWithValue(resource_view::DISK_RESOURCE_NAME, DISK_REQUEST);
    } else if (kind == RequestKind::NUMA) {
        (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
        (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "compact";
    }

    *request->mutable_instance() = std::move(instance);
    request->set_requestid(request->instance().requestid());
    request->set_traceid("benchmark-trace-" + std::to_string(index));
    (*request->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx()->set_maxscore(200);
    return request;
}

struct Result {
    size_t completed{ 0 };
    size_t success{ 0 };
    size_t invalidPlacement{ 0 };
    double qps{ 0.0 };
    double p50Us{ 0.0 };
    double p99Us{ 0.0 };
};

struct SustainedResult {
    Result lifecycle;
    size_t addReports{ 0 };
    size_t deleteReports{ 0 };
    size_t reportFailures{ 0 };
    size_t finalInstances{ 0 };
    uint64_t snapshotPublications{ 0 };
    size_t scheduleRounds{ 0 };
    uint64_t consumedSnapshotSequence{ 0 };
    size_t pendingReservations{ 0 };
    size_t overlayRebuilds{ 0 };
    size_t journalOverflows{ 0 };
    uint64_t consumedMutationSequence{ 0 };
    double reportP50Us{ 0.0 };
    double reportP99Us{ 0.0 };
    uint64_t consumeCalls{ 0 };
    uint64_t consumedRequests{ 0 };
    uint64_t consumeNanos{ 0 };
    uint64_t consumeMaxNanos{ 0 };
    uint64_t consumeMaxRequests{ 0 };
    uint64_t roundBeginCalls{ 0 };
    uint64_t roundBeginNanos{ 0 };
    uint64_t roundBeginMaxNanos{ 0 };
    uint64_t reconcileNanos{ 0 };
    uint64_t reconcileMaxNanos{ 0 };
    uint64_t snapshotSelfYields{ 0 };
    uint64_t legacySelfYields{ 0 };
    uint64_t snapshotBuildCalls{ 0 };
    uint64_t snapshotBuildNanos{ 0 };
    uint64_t snapshotBuildMaxNanos{ 0 };
    uint64_t legacyResourceInfoFetches{ 0 };
    uint64_t legacyResourceInfoFetchesTotal{ 0 };
};

struct SustainedSlot {
    std::shared_ptr<messages::ScheduleRequest> request;
    RequestKind kind;
    Clock::time_point submitted;
    litebus::Future<ScheduleResult> future;
};

class CountingResourceView final : public resource_view::ResourceView {
public:
    CountingResourceView(const std::shared_ptr<resource_view::ResourceViewActor> &actor,
                         std::atomic<uint64_t> &resourceInfoFetches)
        : ResourceView(actor), resourceInfoFetches_(resourceInfoFetches)
    {
    }

    litebus::Future<resource_view::ResourceViewInfo> GetResourceInfo() override
    {
        resourceInfoFetches_.fetch_add(1, std::memory_order_relaxed);
        return ResourceView::GetResourceInfo();
    }

private:
    std::atomic<uint64_t> &resourceInfoFetches_;
};

class DomainSchedulerCurrentPathBenchmark : public ::testing::Test {
public:
    void SetUp() override
    {
        previousLogger_ = observability::api::logs::Provider::GetLoggerProvider();
        observability::api::logs::Provider::SetLoggerProvider(
            std::make_shared<observability::api::logs::NullLoggerProvider>());
    }

    void TearDown() override
    {
        ResetPath();
        observability::api::logs::Provider::SetLoggerProvider(previousLogger_);
    }

    void ResetPath()
    {
        if (queueActor_ != nullptr) {
            litebus::Terminate(queueActor_->GetAID());
            litebus::Await(queueActor_->GetAID());
        }
        if (!resourceViewActorName_.empty()) {
            const litebus::AID resourceViewAid(resourceViewActorName_);
            litebus::Terminate(resourceViewAid);
            litebus::Await(resourceViewAid);
            resourceViewActorName_.clear();
        }
        scheduler_.reset();
        priorityStrategy_.reset();
        unitStrategy_.reset();
        queueActor_.reset();
        resourceView_.reset();
        baseUnits_.clear();
        localRevisions_.clear();
    }

    void SetUpPath(Engine engine, int unitCount, int requestCount, int relaxed,
                   const std::string &aggregation, bool mixed, bool isLocal = false,
                   schedule_decision::AllocateType allocateType = schedule_decision::AllocateType::PRE_ALLOCATION,
                   bool directSchedulingView = true, bool equalFreshness = false)
    {
        engine_ = engine;
        legacyResourceInfoFetches_.store(0, std::memory_order_relaxed);
        const auto suffix = sequence_.fetch_add(1, std::memory_order_relaxed);
        const auto resourceViewID = "current-path-benchmark-" + std::to_string(suffix);
        resourceViewActorName_.clear();

        resource_view::ResourceViewInfo directInfo;
        directInfo.schedulerLevel = resource_view::SCHEDULER_LEVEL::NON_ROOT_DOMAIN;
        directInfo.resourceUnit.set_id("domain-benchmark");
        baseUnits_.clear();
        baseUnits_.reserve(unitCount);
        for (int index = 0; index < unitCount; ++index) {
            auto unit = MakeUnit(index, requestCount, mixed);
            if (isLocal) {
                baseUnits_.emplace_back(std::move(unit));
                continue;
            }

            resource_view::ResourceUnit local;
            local.set_id("benchmark-local-" + std::to_string(index));
            local.set_viewinittime("benchmark-init");
            local.set_revision(0);
            unit.set_ownerid(local.id());
            *local.mutable_capacity() = unit.capacity();
            *local.mutable_allocatable() = unit.allocatable();
            if (directSchedulingView) {
                (*directInfo.resourceUnit.mutable_fragment())[unit.id()] = unit;
                directInfo.allLocalLabels[local.id()] = unit.nodelabels();
            }
            (*local.mutable_fragment())[unit.id()] = std::move(unit);
            localRevisions_[local.id()] = local.revision();
            baseUnits_.emplace_back(std::move(local));
        }

        if (!isLocal && directSchedulingView) {
            resource_view::ResourceViewActor::Param param{
                .isLocal = false,
                .enableTenantAffinity = true,
                .tenantPodReuseTimeWindow = 1,
            };
#if YR_BENCH_HAS_UNIT_SCHEDULER
            param.enableScheduleSnapshot = engine == Engine::UNIT;
#endif
            auto stub = std::make_shared<resource_view::ResourceViewActor>(
                resourceViewID + "-DirectResourceViewActor", resourceViewID, param);
            auto mock = std::make_shared<MockResourceView>(stub);
            EXPECT_CALL(*mock, AddResourceUpdateHandler).WillOnce(::testing::Return());
            if (engine == Engine::LEGACY) {
                EXPECT_CALL(*mock, GetResourceInfo)
                    .WillRepeatedly(::testing::Invoke([this, directInfo]() {
                        legacyResourceInfoFetches_.fetch_add(1, std::memory_order_relaxed);
                        return litebus::Future<resource_view::ResourceViewInfo>(directInfo);
                    }));
            }
            resourceView_ = mock;
#if YR_BENCH_HAS_UNIT_SCHEDULER
            if (engine == Engine::UNIT) {
                resource_view::ScheduleSnapshotDirtySet dirty;
                dirty.structure = true;
                dirty.requestPlacements = true;
                dirty.ownerLabels = true;
                dirty.monopolyIndex = true;
                dirty.metadata = true;
                resource_view::ScheduleSnapshotBuilder builder(resourceView_->GetScheduleSnapshotStore());
                ASSERT_NE(builder.BuildAndPublish(
                              directInfo.resourceUnit, directInfo.schedulerLevel, {},
                              directInfo.allLocalLabels, dirty),
                          nullptr);
            }
#endif
        } else {
            resource_view::ResourceViewActor::Param param{
                .isLocal = isLocal,
                .enableTenantAffinity = true,
                .tenantPodReuseTimeWindow = 1,
            };
#if YR_BENCH_HAS_UNIT_SCHEDULER
            param.enableScheduleSnapshot = engine == Engine::UNIT && !isLocal;
#endif
            resourceViewActorName_ = resourceViewID + "-ResourceViewActor";
            auto actor = std::make_shared<resource_view::ResourceViewActor>(
                resourceViewActorName_, resourceViewID, param);
            litebus::Spawn(actor, false);
            resourceView_ = std::make_shared<CountingResourceView>(actor, legacyResourceInfoFetches_);
            for (const auto &reportedUnit : baseUnits_) {
                const auto status = isLocal
                                        ? resourceView_->AddResourceUnit(reportedUnit).Get()
                                        : resourceView_->AddResourceUnitWithUrl(
                                              reportedUnit, "tcp://127.0.0.1:1").Get();
                ASSERT_TRUE(status.IsOk());
            }
        }

        auto framework = std::make_shared<schedule_framework::FrameworkImpl>(relaxed);
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::prefilter::DefaultPreFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::filter::DefaultFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(
            std::make_shared<prod_plugin::filter::DefaultHeterogeneousFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::filter::ResourceSelectorFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::filter::DiskFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::filter::NUMAAffinityFilter>()));
        ASSERT_TRUE(framework->RegisterPolicy(schedule_framework::PluginFactory::GetInstance().CreatePlugin(
            prod_plugin::RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME)));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::scorer::DefaultScorer>()));
        ASSERT_TRUE(framework->RegisterPolicy(
            std::make_shared<prod_plugin::score::DefaultHeterogeneousScorer>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::score::DiskScorer>()));
        ASSERT_TRUE(framework->RegisterPolicy(std::make_shared<prod_plugin::score::NUMAAffinityScorer>()));
        ASSERT_TRUE(framework->RegisterPolicy(schedule_framework::PluginFactory::GetInstance().CreatePlugin(
            prod_plugin::RELAXED_LABEL_AFFINITY_SCORER_NAME)));

        std::shared_ptr<schedule_decision::ScheduleStrategy> strategy;
#if YR_BENCH_HAS_UNIT_SCHEDULER
        if (engine == Engine::UNIT) {
            unitStrategy_ = std::make_shared<schedule_decision::UnitScheduler>(
                nullptr, 0, PriorityPolicyType::FIFO, aggregation);
            strategy = unitStrategy_;
        } else {
#endif
            strategy = std::make_shared<schedule_decision::PriorityScheduler>(
                nullptr, 0, PriorityPolicyType::FIFO, aggregation);
#if YR_BENCH_HAS_UNIT_SCHEDULER
        }
#endif
        priorityStrategy_ = std::dynamic_pointer_cast<schedule_decision::PriorityScheduler>(strategy);
        ASSERT_NE(priorityStrategy_, nullptr);
        priorityStrategy_->EnableConsumeDiagnostics(true);
        // Placement policy is a workload dimension, not an engine property.
        // Keep Legacy and Unit on identical semantics for a valid A/B.
        strategy->SetPlacementPolicy(BenchmarkPlacementPolicy());
        strategy->RegisterSchedulePerformer(resourceView_, framework, nullptr, allocateType);
        queueActor_ = std::make_shared<ScheduleQueueActor>(
            "CurrentPathBenchmark-" + std::to_string(suffix));
        queueActor_->RegisterScheduler(strategy);
        queueActor_->RegisterResourceView(resourceView_);
        queueActor_->SetAllocateType(allocateType);
        queueActor_->EnableDiagnostics(true);
        if (engine == Engine::LEGACY && equalFreshness) {
            queueActor_->SetLegacyRoundRequestLimit(256);
        }
        resourceView_->GetScheduleSnapshotStore()->EnableDiagnostics(true);
        litebus::Spawn(queueActor_);
        queueActor_->SetNewResourceAvailable();
        scheduler_ = std::make_shared<schedule_decision::Scheduler>(queueActor_->GetAID(), queueActor_->GetAID());
    }

    Result RunClosedLoop(int requestCount, const std::vector<RequestKind> &kinds, int startIndex = 0)
    {
        std::vector<double> latencyUs;
        latencyUs.reserve(requestCount);
        std::vector<std::pair<double, int>> slowSamples;
        const bool traceLatency = startIndex == 0 && std::getenv("DOMAIN_BENCH_TRACE_LATENCY") != nullptr;
        if (traceLatency) {
            slowSamples.reserve(requestCount);
        }
        Result result;
        const auto started = Clock::now();
        for (int index = 0; index < requestCount; ++index) {
            const auto kind = kinds[static_cast<size_t>(index) % kinds.size()];
            const auto begin = Clock::now();
            auto scheduled = scheduler_->ScheduleDecision(
                MakeRequest(startIndex + index, kind)).Get();
            const auto latency = std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
            latencyUs.emplace_back(latency);
            if (traceLatency) {
                slowSamples.emplace_back(latency, index);
            }
            ++result.completed;
            if (scheduled.code == static_cast<int32_t>(StatusCode::SUCCESS)) {
                ++result.success;
                if (!IsValidPlacement(kind, scheduled.unitID)) {
                    ++result.invalidPlacement;
                }
            }
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        result.qps = elapsed == 0 ? 0 : result.completed / elapsed;
        result.p50Us = Percentile(latencyUs, 50);
        result.p99Us = Percentile(latencyUs, 99);
        if (traceLatency) {
            std::sort(slowSamples.begin(), slowSamples.end(), std::greater<>());
            const auto sampleCount = std::min<size_t>(10, slowSamples.size());
            for (size_t index = 0; index < sampleCount; ++index) {
                std::cout << "DOMAIN_SCHEDULER_LATENCY_SAMPLE {\"engine\":\"" << EngineName(engine_)
                          << "\",\"request_index\":" << slowSamples[index].second
                          << ",\"latency_us\":" << slowSamples[index].first << "}" << std::endl;
            }
        }
        return result;
    }

    Result RunOpenLoop(int requestCount, const std::vector<RequestKind> &kinds, int startIndex = 0)
    {
        std::vector<litebus::Future<ScheduleResult>> futures;
        futures.reserve(requestCount);
        const auto started = Clock::now();
        for (int index = 0; index < requestCount; ++index) {
            const auto kind = kinds[static_cast<size_t>(index) % kinds.size()];
            futures.emplace_back(scheduler_->ScheduleDecision(
                MakeRequest(startIndex + index, kind)));
        }
        Result result;
        for (size_t index = 0; index < futures.size(); ++index) {
            auto &future = futures[index];
            const auto scheduled = future.Get();
            ++result.completed;
            if (scheduled.code == static_cast<int32_t>(StatusCode::SUCCESS)) {
                ++result.success;
                const auto kind = kinds[index % kinds.size()];
                if (!IsValidPlacement(kind, scheduled.unitID)) {
                    ++result.invalidPlacement;
                }
            }
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        result.qps = elapsed == 0 ? 0 : result.completed / elapsed;
        return result;
    }

    Result RunOpenLoopAwaitAllocation(int requestCount, int startIndex = 0)
    {
        std::vector<litebus::Future<ScheduleResult>> futures;
        futures.reserve(requestCount);
        const auto started = Clock::now();
        for (int index = 0; index < requestCount; ++index) {
            futures.emplace_back(scheduler_->ScheduleDecision(
                MakeRequest(startIndex + index, RequestKind::PLAIN)));
        }
        Result result;
        for (auto &future : futures) {
            auto scheduled = future.Get();
            ++result.completed;
            if (scheduled.code != static_cast<int32_t>(StatusCode::SUCCESS) ||
                scheduled.allocatedPromise == nullptr) {
                continue;
            }
            if (scheduled.allocatedPromise->GetFuture().Get().IsOk()) {
                ++result.success;
            }
            if (!IsValidPlacement(RequestKind::PLAIN, scheduled.unitID)) {
                ++result.invalidPlacement;
            }
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        result.qps = elapsed == 0 ? 0 : result.completed / elapsed;
        return result;
    }

    resource_view::InstanceInfo MakeReportedInstance(
        const std::shared_ptr<messages::ScheduleRequest> &request, const ScheduleResult &scheduled) const
    {
        auto instance = request->instance();
        const auto required = instance.resources().resources();
        for (const auto &[resourceName, resource] : required) {
            (void)resource;
            if (resource_view::IsHeterogeneousResource(resourceName) ||
                resource_view::IsDiskResource(resourceName)) {
                instance.mutable_resources()->mutable_resources()->erase(resourceName);
            }
        }
        auto *resources = instance.mutable_resources()->mutable_resources();
        for (const auto &[resourceName, allocation] : scheduled.allocatedVectors) {
            auto &resource = (*resources)[resourceName];
            resource.set_name(resourceName);
            resource.set_type(resource_view::ValueType::Value_Type_VECTORS);
            for (const auto &[category, values] : allocation.values()) {
                (*resource.mutable_vectors()->mutable_values())[category] = values;
            }
        }
        for (const auto &allocation : scheduled.vectorAllocations) {
            auto &resource = (*resources)[allocation.type];
            resource.set_name(allocation.type);
            resource.set_type(resource_view::ValueType::Value_Type_VECTORS);
            for (const auto &[category, values] : allocation.allocationValues.values()) {
                (*resource.mutable_vectors()->mutable_values())[category] = values;
            }
        }
        instance.set_unitid(scheduled.unitID);
        instance.add_schedulerchain(scheduled.unitID);
        return instance;
    }

    bool ReportInstanceChange(const std::string &localID, const resource_view::InstanceInfo &instance,
                              decltype(resource_view::InstanceChange::ADD) changeType)
    {
        auto revision = localRevisions_.find(localID);
        if (revision == localRevisions_.end()) {
            return false;
        }
        auto changes = std::make_shared<resource_view::ResourceUnitChanges>();
        changes->set_localid(localID);
        changes->set_localviewinittime("benchmark-init");
        changes->set_startrevision(revision->second);
        changes->set_endrevision(revision->second + 1);
        auto *unitChange = changes->add_changes();
        unitChange->set_resourceunitid(instance.unitid());
        auto *instanceChange = unitChange->mutable_modification()->add_instancechanges();
        instanceChange->set_instanceid(instance.instanceid());
        instanceChange->set_changetype(changeType);
        *instanceChange->mutable_instance() = instance;
        const auto status = resourceView_->UpdateResourceUnitDelta(changes).Get();
        if (status.IsError()) {
            return false;
        }
        ++revision->second;
        return true;
    }

    SustainedResult RunSustainedRealUpdates(int inflight, int measuredCompletions,
                                            const std::vector<RequestKind> &kinds, int startIndex = 0)
    {
        const int warmupCompletions = inflight;
        std::vector<SustainedSlot> slots;
        slots.reserve(inflight);
        int nextRequestIndex = startIndex;
        for (int index = 0; index < inflight; ++index) {
            const auto kind = kinds[static_cast<size_t>(nextRequestIndex) % kinds.size()];
            auto request = MakeRequest(nextRequestIndex++, kind);
            const auto submitted = Clock::now();
            auto future = scheduler_->ScheduleDecision(request);
            slots.emplace_back(SustainedSlot{ std::move(request), kind, submitted, std::move(future) });
        }

        SustainedResult result;
        std::vector<double> lifecycleUs;
        std::vector<double> reportUs;
        lifecycleUs.reserve(measuredCompletions);
        reportUs.reserve(measuredCompletions);
        auto snapshotAtMeasurement = uint64_t{ 0 };
        schedule_decision::ConsumeDiagnostics consumeAtMeasurement;
        schedule_decision::RoundDiagnostics roundAtMeasurement;
        schedule_decision::QueueActorDiagnostics actorAtMeasurement;
        resource_view::SnapshotBuildDiagnostics buildAtMeasurement;
        auto resourceInfoFetchesAtMeasurement = uint64_t{ 0 };
        auto measurementStarted = Clock::now();
        const auto totalCompletions = warmupCompletions + measuredCompletions;
        for (int completion = 0; completion < totalCompletions; ++completion) {
            auto &slot = slots[static_cast<size_t>(completion) % slots.size()];
            const auto scheduled = slot.future.Get();
            const bool measured = completion >= warmupCompletions;
            const auto reportStarted = Clock::now();
            bool addOk = false;
            bool deleteOk = false;
            if (scheduled.code == static_cast<int32_t>(StatusCode::SUCCESS)) {
                const auto instance = MakeReportedInstance(slot.request, scheduled);
                addOk = ReportInstanceChange(scheduled.id, instance, resource_view::InstanceChange::ADD);
                if (addOk) {
                    deleteOk = ReportInstanceChange(scheduled.id, instance, resource_view::InstanceChange::DELETE);
                }
            }
            const auto lifecycleFinished = Clock::now();

            if (measured) {
                ++result.lifecycle.completed;
                if (scheduled.code == static_cast<int32_t>(StatusCode::SUCCESS)) {
                    ++result.lifecycle.success;
                    if (!IsValidPlacement(slot.kind, scheduled.unitID)) {
                        ++result.lifecycle.invalidPlacement;
                    }
                }
                result.addReports += addOk ? 1 : 0;
                result.deleteReports += deleteOk ? 1 : 0;
                result.reportFailures += addOk && deleteOk ? 0 : 1;
                lifecycleUs.emplace_back(
                    std::chrono::duration<double, std::micro>(lifecycleFinished - slot.submitted).count());
                reportUs.emplace_back(
                    std::chrono::duration<double, std::micro>(lifecycleFinished - reportStarted).count());
            }

            const auto kind = kinds[static_cast<size_t>(nextRequestIndex) % kinds.size()];
            auto request = MakeRequest(nextRequestIndex++, kind);
            const auto submitted = Clock::now();
            auto future = scheduler_->ScheduleDecision(request);
            slot = SustainedSlot{ std::move(request), kind, submitted, std::move(future) };

            if (completion + 1 == warmupCompletions) {
                // All ADD/DELETE apply tasks were enqueued before their futures completed.
                // This single read is a measurement-boundary mailbox barrier, not part of the hot path.
                (void)resourceView_->GetResourceViewCopy().Get();
                const auto snapshot = resourceView_->GetScheduleSnapshotStore()->Load();
                snapshotAtMeasurement = snapshot == nullptr ? 0 : snapshot->publicationSequence;
                consumeAtMeasurement = priorityStrategy_->GetConsumeDiagnostics();
                if (unitStrategy_ != nullptr) {
                    roundAtMeasurement = unitStrategy_->GetRoundDiagnostics();
                }
                actorAtMeasurement = queueActor_->GetDiagnostics();
                buildAtMeasurement = resourceView_->GetScheduleSnapshotStore()->GetBuildDiagnostics();
                resourceInfoFetchesAtMeasurement = legacyResourceInfoFetches_.load(std::memory_order_relaxed);
                measurementStarted = Clock::now();
            }
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - measurementStarted).count();
        result.lifecycle.qps = elapsed == 0 ? 0 : result.lifecycle.completed / elapsed;
        result.lifecycle.p50Us = Percentile(lifecycleUs, 50);
        result.lifecycle.p99Us = Percentile(lifecycleUs, 99);
        result.reportP50Us = Percentile(reportUs, 50);
        result.reportP99Us = Percentile(reportUs, 99);

        // Drain all previously accepted DELETE updates and validate that the physical view converged.
        const auto finalView = resourceView_->GetResourceViewCopy().Get();
        result.finalInstances = static_cast<size_t>(finalView->instances_size());
        for (const auto &[unitID, unit] : finalView->fragment()) {
            (void)unitID;
            result.finalInstances += static_cast<size_t>(unit.instances_size());
        }
        const auto finalSnapshot = resourceView_->GetScheduleSnapshotStore()->Load();
        if (finalSnapshot != nullptr && finalSnapshot->publicationSequence >= snapshotAtMeasurement) {
            result.snapshotPublications = finalSnapshot->publicationSequence - snapshotAtMeasurement;
        }
        if (unitStrategy_ != nullptr) {
            result.scheduleRounds = unitStrategy_->ScheduleRoundCount();
            result.consumedSnapshotSequence = unitStrategy_->ConsumedSnapshotSequence();
            result.pendingReservations = unitStrategy_->ReservationCount();
            result.overlayRebuilds = unitStrategy_->OverlayRebuildCount();
            result.journalOverflows = unitStrategy_->JournalOverflowCount();
            result.consumedMutationSequence = unitStrategy_->ConsumedMutationSequence();
        }
        const auto consume = priorityStrategy_->GetConsumeDiagnostics();
        result.consumeCalls = consume.calls - consumeAtMeasurement.calls;
        result.consumedRequests = consume.requests - consumeAtMeasurement.requests;
        result.consumeNanos = consume.totalNanos - consumeAtMeasurement.totalNanos;
        result.consumeMaxNanos = consume.maxNanos;
        result.consumeMaxRequests = consume.maxRequests;
        if (unitStrategy_ != nullptr) {
            const auto round = unitStrategy_->GetRoundDiagnostics();
            result.roundBeginCalls = round.calls - roundAtMeasurement.calls;
            result.roundBeginNanos = round.totalNanos - roundAtMeasurement.totalNanos;
            result.roundBeginMaxNanos = round.maxNanos;
            result.reconcileNanos = round.reconcileNanos - roundAtMeasurement.reconcileNanos;
            result.reconcileMaxNanos = round.maxReconcileNanos;
        }
        const auto actor = queueActor_->GetDiagnostics();
        result.snapshotSelfYields = actor.snapshotSelfYields - actorAtMeasurement.snapshotSelfYields;
        result.legacySelfYields = actor.legacySelfYields - actorAtMeasurement.legacySelfYields;
        const auto build = resourceView_->GetScheduleSnapshotStore()->GetBuildDiagnostics();
        result.snapshotBuildCalls = build.calls - buildAtMeasurement.calls;
        result.snapshotBuildNanos = build.totalNanos - buildAtMeasurement.totalNanos;
        result.snapshotBuildMaxNanos = build.maxNanos;
        result.legacyResourceInfoFetchesTotal = legacyResourceInfoFetches_.load(std::memory_order_relaxed);
        result.legacyResourceInfoFetches =
            result.legacyResourceInfoFetchesTotal - resourceInfoFetchesAtMeasurement;
        return result;
    }

    std::thread StartUpdater(int updatesPerSecond, std::atomic<bool> &stop, std::atomic<uint64_t> &accepted)
    {
        return std::thread([this, updatesPerSecond, &stop, &accepted]() {
            const auto interval = std::chrono::microseconds(1000000 / updatesPerSecond);
            auto next = Clock::now();
            size_t update = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                const auto &reportedLocal = baseUnits_[update % baseUnits_.size()];
                auto unit = std::make_shared<resource_view::ResourceUnit>(
                    reportedLocal.fragment().empty() ? reportedLocal : reportedLocal.fragment().begin()->second);
                auto &noise = unit->mutable_capacity()->mutable_resources()->at(UPDATE_NOISE_RESOURCE);
                const auto cycle = update / baseUnits_.size();
                noise.mutable_scalar()->set_value(1000000.0 + (cycle % 2 == 0 ? 1.0 : 0.0));
                if (resourceView_->UpdateResourceUnit(unit, resource_view::UpdateType::UPDATE_DYNAMIC).Get().IsOk()) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
                ++update;
                next += interval;
                std::this_thread::sleep_until(next);
            }
        });
    }

    void Print(const char *workload, Engine engine, const std::string &aggregation,
               int relaxed, int updateRate, uint64_t updates, const Result &result)
    {
        const auto *engineName = engine == Engine::UNIT && std::string(workload).rfind("local_", 0) == 0
                                     ? "unit_mailbox"
                                     : EngineName(engine);
        std::cout << "DOMAIN_SCHEDULER_CURRENT_PATH_BENCH " << std::fixed << std::setprecision(3)
                  << "{\"workload\":\"" << workload << "\",\"engine\":\"" << engineName
                  << "\",\"aggregation\":\"" << aggregation << "\",\"schedule_relaxed\":" << relaxed
                  << ",\"update_reports_per_sec\":" << updateRate << ",\"updates_applied\":" << updates
                  << ",\"completed\":" << result.completed << ",\"success\":" << result.success
                  << ",\"invalid_placement\":" << result.invalidPlacement << ",\"qps\":" << result.qps
                  << ",\"p50_us\":" << result.p50Us << ",\"p99_us\":" << result.p99Us << "}" << std::endl;
    }

    void PrintSustained(Engine engine, const std::string &aggregation, int inflight, bool equalFreshness,
                        const SustainedResult &result)
    {
        std::cout << "DOMAIN_SCHEDULER_SUSTAINED_UPDATE_BENCH " << std::fixed << std::setprecision(3)
                  << "{\"engine\":\"" << EngineName(engine) << "\",\"aggregation\":\"" << aggregation
                  << "\",\"placement_policy\":\"" << BenchmarkPlacementPolicy()
                  << "\",\"equal_freshness\":" << (equalFreshness ? "true" : "false")
                  << ",\"inflight\":" << inflight << ",\"completed\":" << result.lifecycle.completed
                  << ",\"success\":" << result.lifecycle.success
                  << ",\"invalid_placement\":" << result.lifecycle.invalidPlacement
                  << ",\"add_reports\":" << result.addReports
                  << ",\"delete_reports\":" << result.deleteReports
                  << ",\"report_failures\":" << result.reportFailures
                  << ",\"final_instances\":" << result.finalInstances
                  << ",\"snapshot_publications\":" << result.snapshotPublications
                  << ",\"schedule_rounds\":" << result.scheduleRounds
                  << ",\"consumed_snapshot_sequence\":" << result.consumedSnapshotSequence
                  << ",\"pending_reservations\":" << result.pendingReservations
                  << ",\"overlay_rebuilds\":" << result.overlayRebuilds
                  << ",\"journal_overflows\":" << result.journalOverflows
                  << ",\"consumed_mutation_sequence\":" << result.consumedMutationSequence
                  << ",\"lifecycle_qps\":" << result.lifecycle.qps
                  << ",\"lifecycle_p50_us\":" << result.lifecycle.p50Us
                  << ",\"lifecycle_p99_us\":" << result.lifecycle.p99Us
                  << ",\"report_p50_us\":" << result.reportP50Us
                  << ",\"report_p99_us\":" << result.reportP99Us
                  << ",\"consume_calls\":" << result.consumeCalls
                  << ",\"consumed_requests\":" << result.consumedRequests
                  << ",\"consume_total_us\":" << result.consumeNanos / 1000.0
                  << ",\"consume_avg_us\":"
                  << (result.consumeCalls == 0 ? 0.0 : result.consumeNanos / 1000.0 / result.consumeCalls)
                  << ",\"consume_max_us\":" << result.consumeMaxNanos / 1000.0
                  << ",\"consume_avg_batch\":"
                  << (result.consumeCalls == 0 ? 0.0
                                               : static_cast<double>(result.consumedRequests) / result.consumeCalls)
                  << ",\"consume_max_batch\":" << result.consumeMaxRequests
                  << ",\"round_begin_calls\":" << result.roundBeginCalls
                  << ",\"round_begin_total_us\":" << result.roundBeginNanos / 1000.0
                  << ",\"round_begin_max_us\":" << result.roundBeginMaxNanos / 1000.0
                  << ",\"reconcile_total_us\":" << result.reconcileNanos / 1000.0
                  << ",\"reconcile_max_us\":" << result.reconcileMaxNanos / 1000.0
                  << ",\"snapshot_self_yields\":" << result.snapshotSelfYields
                  << ",\"legacy_self_yields\":" << result.legacySelfYields
                  << ",\"snapshot_build_calls\":" << result.snapshotBuildCalls
                  << ",\"snapshot_build_total_us\":" << result.snapshotBuildNanos / 1000.0
                  << ",\"snapshot_build_max_us\":" << result.snapshotBuildMaxNanos / 1000.0
                  << ",\"legacy_resource_info_fetches\":" << result.legacyResourceInfoFetches
                  << ",\"legacy_resource_info_fetches_total\":" << result.legacyResourceInfoFetchesTotal
                  << "}" << std::endl;
    }

private:
    static std::atomic<uint64_t> sequence_;
    std::shared_ptr<observability::api::logs::LoggerProvider> previousLogger_;
    std::shared_ptr<resource_view::ResourceView> resourceView_;
    std::shared_ptr<ScheduleQueueActor> queueActor_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::shared_ptr<schedule_decision::PriorityScheduler> priorityStrategy_;
    std::shared_ptr<schedule_decision::UnitScheduler> unitStrategy_;
    std::vector<resource_view::ResourceUnit> baseUnits_;
    std::unordered_map<std::string, uint64_t> localRevisions_;
    std::string resourceViewActorName_;
    Engine engine_{ Engine::LEGACY };
    std::atomic<uint64_t> legacyResourceInfoFetches_{ 0 };
};

std::atomic<uint64_t> DomainSchedulerCurrentPathBenchmark::sequence_{ 0 };

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportResourceUpdateTailAB)
{
    constexpr int unitCount = 1000;
    const int requestCount = BenchmarkRequestCount(1000);
    for (const int relaxed : { -1, 1, 32 }) {
        if (!MatchesBenchmarkFilter("DOMAIN_BENCH_RELAXED", std::to_string(relaxed).c_str())) {
            continue;
        }
        for (const int updateRate : { 0, 100, 500 }) {
            if (!MatchesBenchmarkFilter("DOMAIN_BENCH_UPDATE_RATE", std::to_string(updateRate).c_str())) {
                continue;
            }
            for (const auto engine : BenchmarkEngines()) {
                if (!MatchesBenchmarkFilter("DOMAIN_BENCH_ENGINE", EngineName(engine))) {
                    continue;
                }
                SetUpPath(engine, unitCount, requestCount + 30, relaxed, "no_aggregate", false, false,
                          schedule_decision::AllocateType::PRE_ALLOCATION, false);
                const auto warmup = RunClosedLoop(30, { RequestKind::PLAIN }, 1000000);
                ASSERT_EQ(warmup.success, 30u);
                std::atomic<bool> stop{ false };
                std::atomic<uint64_t> updates{ 0 };
                std::thread updater;
                if (updateRate > 0) {
                    updater = StartUpdater(updateRate, stop, updates);
                }
                const auto result = RunClosedLoop(requestCount, { RequestKind::PLAIN });
                stop.store(true, std::memory_order_relaxed);
                if (updater.joinable()) {
                    updater.join();
                }
                EXPECT_EQ(result.success, static_cast<size_t>(requestCount));
                EXPECT_EQ(result.invalidPlacement, 0u);
                Print("resource_update_tail", engine, "no_aggregate", relaxed, updateRate,
                      updates.load(), result);
                ResetPath();
            }
        }
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, StorageAndNpuCombinationTeardownRegression)
{
    constexpr int unitCount = 1000;
    constexpr int requestCount = 400;
    const std::vector<RequestKind> kinds{ RequestKind::NPU, RequestKind::STORAGE };
    for (const auto engine : BenchmarkEngines()) {
        SetUpPath(engine, unitCount, requestCount + 30, 32, "no_aggregate", true);
        const auto warmup = RunClosedLoop(30, kinds, 1000000);
        ASSERT_EQ(warmup.success, 30u);

        const auto result = RunOpenLoop(requestCount, kinds);
        EXPECT_EQ(result.completed, static_cast<size_t>(requestCount));
        EXPECT_EQ(result.success, static_cast<size_t>(requestCount));
        EXPECT_EQ(result.invalidPlacement, 0u);
        ResetPath();
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, AdvancedGroupAndRangeSemanticsMatchLegacyAndUnitSnapshot)
{
    struct GroupOutcome {
        int32_t code;
        std::vector<std::string> units;
        size_t pendingReservations;
    };

    auto runGroup = [this](Engine engine, int unitCount, int capacityPerUnit, int requestCount,
                           const schedule_decision::GroupSpec::RangeOpt &range,
                           common::GroupPolicy policy, int startIndex) {
        SetUpPath(engine, unitCount, capacityPerUnit, 32, "relaxed", false);
        auto spec = std::make_shared<schedule_decision::GroupSpec>();
        spec->groupReqId = "advanced-group-" + std::to_string(startIndex);
        spec->rangeOpt = range;
        spec->priority = false;
        spec->timeout = 1;
        spec->groupSchedulePolicy = policy;
        for (int index = 0; index < requestCount; ++index) {
            spec->requests.emplace_back(MakeRequest(startIndex + index, RequestKind::PLAIN));
        }

        const auto result = scheduler_->GroupScheduleDecision(spec).Get();
        GroupOutcome outcome{ result.code, {}, 0 };
        for (const auto &scheduled : result.results) {
            if (scheduled.code == static_cast<int32_t>(StatusCode::SUCCESS)) {
                outcome.units.emplace_back(scheduled.unitID);
            }
        }
        if (unitStrategy_ != nullptr) {
            if (result.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
                for (int retry = 0; retry < 1000 && unitStrategy_->ReservationCount() != 0; ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            outcome.pendingReservations = unitStrategy_->ReservationCount();
        }
        ResetPath();
        return outcome;
    };

    const auto fixed = schedule_decision::GroupSpec::RangeOpt{
        .isRange = false,
        .min = 0,
        .max = 0,
        .step = 0,
    };
    const auto range = schedule_decision::GroupSpec::RangeOpt{
        .isRange = true,
        .min = 5,
        .max = 10,
        .step = 2,
    };

    const auto legacyPack = runGroup(Engine::LEGACY, 3, 4, 3, fixed, common::GroupPolicy::StrictPack, 3000000);
    const auto unitPack = runGroup(Engine::UNIT, 3, 4, 3, fixed, common::GroupPolicy::StrictPack, 3000100);
    ASSERT_EQ(legacyPack.code, static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_EQ(unitPack.code, legacyPack.code);
    ASSERT_EQ(legacyPack.units.size(), 3u);
    ASSERT_EQ(unitPack.units.size(), legacyPack.units.size());
    EXPECT_EQ(std::set<std::string>(legacyPack.units.begin(), legacyPack.units.end()).size(), 1u);
    EXPECT_EQ(std::set<std::string>(unitPack.units.begin(), unitPack.units.end()).size(), 1u);
    // StrictPack is scheduled as one virtual request carrying the group's total
    // resources, so the overlay intentionally keeps one aggregate reservation.
    EXPECT_EQ(unitPack.pendingReservations, 1u);

    const auto legacyGang = runGroup(Engine::LEGACY, 3, 1, 3, fixed, common::GroupPolicy::None, 3000200);
    const auto unitGang = runGroup(Engine::UNIT, 3, 1, 3, fixed, common::GroupPolicy::None, 3000300);
    ASSERT_EQ(legacyGang.code, static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_EQ(unitGang.code, legacyGang.code);
    ASSERT_EQ(legacyGang.units.size(), 3u);
    ASSERT_EQ(unitGang.units.size(), legacyGang.units.size());
    EXPECT_EQ(std::set<std::string>(legacyGang.units.begin(), legacyGang.units.end()).size(), 3u);
    EXPECT_EQ(std::set<std::string>(unitGang.units.begin(), unitGang.units.end()).size(), 3u);
    EXPECT_EQ(unitGang.pendingReservations, unitGang.units.size());

    const auto legacyRange = runGroup(Engine::LEGACY, 1, 7, 10, range, common::GroupPolicy::None, 3000400);
    const auto unitRange = runGroup(Engine::UNIT, 1, 7, 10, range, common::GroupPolicy::None, 3000500);
    ASSERT_EQ(legacyRange.code, static_cast<int32_t>(StatusCode::SUCCESS));
    ASSERT_EQ(unitRange.code, legacyRange.code);
    ASSERT_EQ(legacyRange.units.size(), 6u);
    ASSERT_EQ(unitRange.units.size(), legacyRange.units.size());
    EXPECT_EQ(unitRange.pendingReservations, unitRange.units.size());

    const auto legacyGangFailure =
        runGroup(Engine::LEGACY, 1, 2, 3, fixed, common::GroupPolicy::None, 3000600);
    const auto unitGangFailure =
        runGroup(Engine::UNIT, 1, 2, 3, fixed, common::GroupPolicy::None, 3000700);
    EXPECT_EQ(legacyGangFailure.code, static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH));
    EXPECT_EQ(unitGangFailure.code, legacyGangFailure.code);
    EXPECT_EQ(unitGangFailure.pendingReservations, 0u);
}

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportResourceUpdateApplyCostAB)
{
    constexpr int unitCount = 1000;
    constexpr int updateCount = 1000;
    for (const auto engine : BenchmarkEngines()) {
        SetUpPath(engine, unitCount, 30, 32, "no_aggregate", false, false,
                  schedule_decision::AllocateType::PRE_ALLOCATION, false);
        const auto started = Clock::now();
        size_t success = 0;
        for (int update = 0; update < updateCount; ++update) {
            const auto &reportedLocal = baseUnits_[static_cast<size_t>(update) % baseUnits_.size()];
            auto unit = std::make_shared<resource_view::ResourceUnit>(
                reportedLocal.fragment().empty() ? reportedLocal : reportedLocal.fragment().begin()->second);
            auto &noise = unit->mutable_capacity()->mutable_resources()->at(UPDATE_NOISE_RESOURCE);
            const auto cycle = static_cast<size_t>(update) / baseUnits_.size();
            noise.mutable_scalar()->set_value(1000000.0 + (cycle % 2 == 0 ? 1.0 : 0.0));
            if (resourceView_->UpdateResourceUnit(unit, resource_view::UpdateType::UPDATE_DYNAMIC).Get().IsOk()) {
                ++success;
            }
        }
        const auto elapsedUs = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
        std::cout << "DOMAIN_SCHEDULER_UPDATE_APPLY_BENCH {\"engine\":\"" << EngineName(engine)
                  << "\",\"units\":" << unitCount << ",\"updates\":" << updateCount
                  << ",\"success\":" << success << ",\"total_us\":" << elapsedUs
                  << ",\"mean_us\":" << elapsedUs / updateCount << "}" << std::endl;
        EXPECT_EQ(success, static_cast<size_t>(updateCount));
        ResetPath();
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportSustainedScheduleUpdateDeleteAB)
{
    constexpr int unitCount = 1000;
    const int inflight = BenchmarkInflight(5000);
    const int measuredCompletions = BenchmarkRequestCount(5000);
    const int capacityPerUnit = std::max(8, inflight / unitCount * 4);
    const bool equalFreshness = BenchmarkEqualFreshness();
    for (const auto engine : BenchmarkEngines()) {
        if (!MatchesBenchmarkFilter("DOMAIN_BENCH_ENGINE", EngineName(engine))) {
            continue;
        }
        for (const std::string aggregation : { "no_aggregate", "relaxed" }) {
            if (!MatchesBenchmarkFilter("DOMAIN_BENCH_AGGREGATION", aggregation.c_str())) {
                continue;
            }
            SetUpPath(engine, unitCount, capacityPerUnit, 32, aggregation, false, false,
                      schedule_decision::AllocateType::PRE_ALLOCATION, false, equalFreshness);
            const auto result = RunSustainedRealUpdates(
                inflight, measuredCompletions, { RequestKind::PLAIN }, 2000000);
            EXPECT_EQ(result.lifecycle.invalidPlacement, 0u);
            EXPECT_EQ(result.addReports, result.lifecycle.success);
            EXPECT_EQ(result.deleteReports, result.lifecycle.success);
            EXPECT_EQ(result.reportFailures, result.lifecycle.completed - result.lifecycle.success);
            if (engine == Engine::UNIT) {
                EXPECT_EQ(result.lifecycle.success, static_cast<size_t>(measuredCompletions));
                EXPECT_EQ(result.journalOverflows, 0u);
            }
            EXPECT_EQ(result.finalInstances, 0u);
            PrintSustained(engine, aggregation, inflight, equalFreshness, result);
            ResetPath();
        }
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, LocalConflictRetryRollsBackAndAvoidsFailedUnit)
{
    constexpr int unitCount = 2;
    for (const auto engine : BenchmarkEngines()) {
        if (!MatchesBenchmarkFilter("DOMAIN_BENCH_ENGINE", EngineName(engine))) {
            continue;
        }
        SetUpPath(engine, unitCount, 1, 1, "no_aggregate", false, false,
                  schedule_decision::AllocateType::PRE_ALLOCATION, true, true);
        auto request = MakeRequest(0, RequestKind::PLAIN);
        const auto first = scheduler_->ScheduleDecision(request).Get();
        ASSERT_EQ(first.code, static_cast<int32_t>(StatusCode::SUCCESS));
        const auto started = Clock::now();
        const auto retried = scheduler_->RetryScheduleDecision(
            request, litebus::Future<std::string>(), first.unitID).Get();
        const auto retryUs = std::chrono::duration<double, std::micro>(Clock::now() - started).count();
        EXPECT_EQ(retried.code, static_cast<int32_t>(StatusCode::SUCCESS));
        EXPECT_NE(retried.unitID, first.unitID);
        const auto reservations = unitStrategy_ == nullptr ? 1 : unitStrategy_->ReservationCount();
        EXPECT_EQ(reservations, 1u);
        std::cout << "DOMAIN_SCHEDULER_CONFLICT_RETRY_BENCH {\"engine\":\"" << EngineName(engine)
                  << "\",\"first_unit\":\"" << first.unitID << "\",\"retry_unit\":\""
                  << retried.unitID << "\",\"retry_us\":" << retryUs
                  << ",\"pending_reservations\":" << reservations << "}" << std::endl;
        ResetPath();
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportConflictRetryStormAB)
{
    constexpr int unitCount = 1000;
    const int requestCount = BenchmarkRequestCount(1000);
    for (const auto engine : BenchmarkEngines()) {
        if (!MatchesBenchmarkFilter("DOMAIN_BENCH_ENGINE", EngineName(engine))) {
            continue;
        }
        SetUpPath(engine, unitCount, 4, 32, "no_aggregate", false, false,
                  schedule_decision::AllocateType::PRE_ALLOCATION, true, true);
        std::vector<double> retryLatencyUs;
        retryLatencyUs.reserve(requestCount);
        size_t changedUnit = 0;
        const auto started = Clock::now();
        for (int index = 0; index < requestCount; ++index) {
            auto request = MakeRequest(index, RequestKind::PLAIN);
            const auto first = scheduler_->ScheduleDecision(request).Get();
            ASSERT_EQ(first.code, static_cast<int32_t>(StatusCode::SUCCESS));
            const auto retryStarted = Clock::now();
            const auto retried = scheduler_->RetryScheduleDecision(
                request, litebus::Future<std::string>(), first.unitID).Get();
            retryLatencyUs.emplace_back(
                std::chrono::duration<double, std::micro>(Clock::now() - retryStarted).count());
            ASSERT_EQ(retried.code, static_cast<int32_t>(StatusCode::SUCCESS));
            changedUnit += retried.unitID == first.unitID ? 0 : 1;
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        const auto reservations = unitStrategy_ == nullptr ? static_cast<size_t>(requestCount)
                                                           : unitStrategy_->ReservationCount();
        EXPECT_EQ(changedUnit, static_cast<size_t>(requestCount));
        EXPECT_EQ(reservations, static_cast<size_t>(requestCount));
        std::cout << "DOMAIN_SCHEDULER_CONFLICT_STORM_BENCH {\"engine\":\"" << EngineName(engine)
                  << "\",\"requests\":" << requestCount << ",\"changed_unit\":" << changedUnit
                  << ",\"qps\":" << (elapsed == 0 ? 0 : requestCount / elapsed)
                  << ",\"retry_p50_us\":" << Percentile(retryLatencyUs, 50)
                  << ",\"retry_p99_us\":" << Percentile(retryLatencyUs, 99)
                  << ",\"pending_reservations\":" << reservations << "}" << std::endl;
        ResetPath();
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportAggregationAndMixedAB)
{
    constexpr int unitCount = 1000;
    const int requestCount = BenchmarkRequestCount(1000);
    const std::vector<RequestKind> allMixedKinds{
        RequestKind::PLAIN, RequestKind::REQUIRED_AFFINITY, RequestKind::WEAK_ANTI_AFFINITY,
        RequestKind::NPU, RequestKind::STORAGE, RequestKind::NUMA,
    };
    std::vector<RequestKind> mixed;
    for (const auto kind : allMixedKinds) {
        if (MatchesBenchmarkFilter("DOMAIN_BENCH_KIND", RequestKindName(kind))) {
            mixed.emplace_back(kind);
        }
    }
    ASSERT_FALSE(mixed.empty());
    for (const bool mixedWorkload : { false, true }) {
        if (!MatchesBenchmarkFilter("DOMAIN_BENCH_WORKLOAD", mixedWorkload ? "mixed" : "homogeneous")) {
            continue;
        }
        for (const auto engine : BenchmarkEngines()) {
            if (!MatchesBenchmarkFilter("DOMAIN_BENCH_ENGINE", EngineName(engine))) {
                continue;
            }
            for (const std::string aggregation : { "no_aggregate", "relaxed" }) {
                if (!MatchesBenchmarkFilter("DOMAIN_BENCH_AGGREGATION", aggregation.c_str())) {
                    continue;
                }
                SetUpPath(engine, unitCount, requestCount + 30, 32, aggregation, mixedWorkload);
                const auto warmup = RunClosedLoop(30, mixedWorkload ? mixed : std::vector<RequestKind>{ RequestKind::PLAIN },
                                                  1000000);
                ASSERT_EQ(warmup.success, 30u);
                const auto result = RunOpenLoop(
                    requestCount, mixedWorkload ? mixed : std::vector<RequestKind>{ RequestKind::PLAIN });
                if (engine == Engine::UNIT || aggregation == "no_aggregate") {
                    EXPECT_EQ(result.success, static_cast<size_t>(requestCount));
                    EXPECT_EQ(result.invalidPlacement, 0u);
                }
                Print(mixedWorkload ? "mixed" : "homogeneous", engine, aggregation, 32, 0, 0, result);
                ResetPath();
            }
        }
    }
}

TEST_F(DomainSchedulerCurrentPathBenchmark, DISABLED_ReportLocalAllocationAB)
{
    const int requestCount = BenchmarkRequestCount(1000);
    for (const int unitCount : { 1, 8, 64 }) {
        for (const auto engine : BenchmarkEngines()) {
            SetUpPath(engine, unitCount, requestCount + 30, 1, "no_aggregate", false, true,
                      schedule_decision::AllocateType::ALLOCATION);
            const auto result = RunOpenLoopAwaitAllocation(requestCount);
            EXPECT_EQ(result.completed, static_cast<size_t>(requestCount));
            EXPECT_EQ(result.success, static_cast<size_t>(requestCount));
            EXPECT_EQ(result.invalidPlacement, 0u);
            Print(("local_allocation_" + std::to_string(unitCount) + "_units").c_str(), engine,
                  "no_aggregate", 1, 0, 0, result);
            ResetPath();
        }
    }
}

}  // namespace
}  // namespace functionsystem::test
