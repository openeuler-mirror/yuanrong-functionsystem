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
#include <iostream>
#include <gtest/gtest.h>

#include "exporters/file_exporter/include/json_parser.h"

namespace observability::test {

TEST(JSONParserTest, simple_serialize)
{
    const observability::metrics::MetricsData metricsData = {
        .labels = {},
        .name = "name1",
        .description = "name1",
        .unit = "name1",
        .metricType = "Gauge",
        .collectTimeStamp = std::chrono::system_clock::now(),
        .metricValue = observability::metrics::MetricValue(0.1)
    };

    auto parser = std::make_shared<observability::metrics::JsonParser>();

    std::cout << parser->Serialize(metricsData) << std::endl;

}

}