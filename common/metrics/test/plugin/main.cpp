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

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "metrics/plugin/dynamic_load.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: load_plugin <plugin>\n";
        return -1;
    }
    std::string error;
    auto factory = observability::plugin::metrics::LoadFactory(argv[1], error);
    if (factory == nullptr) {
        std::cerr << "Failed to load plugin: " << error << "\n";
        return -1;
    }

    std::string config{ "init config" };
    auto exporter = factory->MakeExpoter(config, error);
    if (exporter == nullptr) {
        std::cerr << "Failed to make tracer: " << error << "\n";
        return -1;
    }
    observability::sdk::metrics::MetricData data;
    data.collectionTs = std::chrono::system_clock::now();
    std::vector<observability::sdk::metrics::MetricData> dataVec = { data };
    exporter->Export(dataVec);
    return 0;
}