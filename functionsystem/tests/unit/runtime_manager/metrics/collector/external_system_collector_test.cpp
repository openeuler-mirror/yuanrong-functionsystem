/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "runtime_manager/metrics/collector/external_system_collector.h"

#include <gtest/gtest.h>

#include "common/resource_view/resource_type.h"

namespace functionsystem::runtime_manager {

TEST(ExternalSystemXPUCollectorTest, ParsesGpuAndNpuIndependently)
{
    const std::string response = R"({"xpu":[
        {"type":"gpu","product_model":"L20","device_ids":[0,2]},
        {"type":"npu","product_model":"Ascend910B4","device_ids":[1,3]}
    ]})";

    auto gpu = ParseExternalXpuMetric(response, "gpu");
    auto npu = ParseExternalXpuMetric(response, "npu");

    ASSERT_TRUE(gpu.IsSome());
    ASSERT_TRUE(npu.IsSome());
    ASSERT_TRUE(gpu.Get().devClusterMetrics.IsSome());
    ASSERT_TRUE(npu.Get().devClusterMetrics.IsSome());
    EXPECT_EQ(gpu.Get().devClusterMetrics.Get().strInfo.at(dev_metrics_type::PRODUCT_MODEL_KEY), "l20");
    EXPECT_EQ(npu.Get().devClusterMetrics.Get().strInfo.at(dev_metrics_type::PRODUCT_MODEL_KEY), "ascend910b4");
    EXPECT_EQ(npu.Get().devClusterMetrics.Get().intsInfo.at(resource_view::IDS_KEY), (std::vector<int>{1, 3}));
}

TEST(ExternalSystemXPUCollectorTest, ValidAbsenceClearsOnlyRequestedType)
{
    auto parsed = ParseExternalXpuMetric(
        R"({"xpu":[{"type":"gpu","product_model":"l20","device_ids":[0]}]})", "npu");

    ASSERT_TRUE(parsed.IsSome());
    EXPECT_FALSE(parsed.Get().devClusterMetrics.IsSome());
    EXPECT_TRUE(parsed.Get().value.IsNone());
}

TEST(ExternalSystemXPUCollectorTest, RejectsMalformedDuplicateOrMultipleEntries)
{
    EXPECT_TRUE(ParseExternalXpuMetric(
                    R"({"xpu":[{"type":"npu","product_model":"ascend910b4","device_ids":[0,0]}]})",
                    "npu")
                    .IsNone());
    EXPECT_TRUE(ParseExternalXpuMetric(
                    R"({"xpu":[{"type":"npu","product_model":"a","device_ids":[0]},
                    {"type":"npu","product_model":"a","device_ids":[1]}]})",
                    "npu")
                    .IsNone());
    EXPECT_TRUE(ParseExternalXpuMetric("not-json", "npu").IsNone());
    EXPECT_TRUE(ParseExternalXpuMetric(
                    R"({"xpu":[{"type":"fpga","product_model":"x","device_ids":[0]}]})",
                    "npu")
                    .IsNone());
}

}  // namespace functionsystem::runtime_manager
