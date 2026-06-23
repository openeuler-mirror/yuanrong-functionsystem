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

#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "utils/os_utils.hpp"

#define private public
#include "runtime_manager/executor/sandbox/sandbox_executor.h"
#undef private

namespace functionsystem::test {

using namespace functionsystem::runtime_manager;

namespace {

std::string GetMetricsFilesName(const std::string & /*backendName*/)
{
    return "sandbox-executor-metrics-test.data";
}

messages::RuntimeInstanceInfo BuildRuntimeInstanceInfo()
{
    messages::RuntimeInstanceInfo info;
    info.set_instanceid("instance-1");
    info.set_runtimeid("runtime-1");
    info.set_requestid("request-1");
    info.mutable_container()->set_runtime("runsc");
    info.mutable_container()->mutable_rootfsconfig()->set_type(runtime::v1::RootfsSrcType::IMAGE);
    info.mutable_container()->mutable_rootfsconfig()->set_image_url("swr.cn-east-3.example/sandbox:v1");

    auto *cpu = &(*info.mutable_runtimeconfig()->mutable_resources()->mutable_resources())["CPU"];
    cpu->set_name("CPU");
    cpu->set_type(resource_view::ValueType::Value_Type_SCALAR);
    cpu->mutable_scalar()->set_value(500);
    cpu->mutable_scalar()->set_limit(1500);

    auto *memory = &(*info.mutable_runtimeconfig()->mutable_resources()->mutable_resources())["Memory"];
    memory->set_name("Memory");
    memory->set_type(resource_view::ValueType::Value_Type_SCALAR);
    memory->mutable_scalar()->set_value(128);
    memory->mutable_scalar()->set_limit(256);

    return info;
}

}  // namespace

class SandboxExecutorMetricsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        litebus::os::SetEnv("YR_SANDBOX_METRICS_ENABLED", "1");
        metrics::MetricsAdapter::GetInstance().InitMetricsFromJson(nlohmann::json::parse(R"(
        {
          "backends": [
            {
              "immediatelyExport": {
                "name": "file",
                "enable": true,
                "exporters": [
                  {
                    "fileExporter": {
                      "enable": true,
                      "fileDir": "/tmp/",
                      "rolling": {
                        "enable": true,
                        "maxFiles": 1,
                        "maxSize": 10000
                      },
                      "contentType": "STANDARD"
                    }
                  }
                ]
              }
            }
          ]
        })"), GetMetricsFilesName, {});
        metrics::MetricsAdapter::GetInstance().SetContextAttr("node_id", "node-a");
        metrics::MetricsAdapter::GetInstance().SetContextAttr("ip", "127.0.0.1");
    }

    void TearDown() override
    {
        metrics::MetricsAdapter::GetInstance().CleanMetrics();
        metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.clear();
        litebus::os::UnSetEnv("YR_SANDBOX_METRICS_ENABLED");
    }
};

TEST_F(SandboxExecutorMetricsTest, DoReportMetricsPublishesLifecycleAndRequestedResourceGauges)
{
    SandboxExecutor executor("sandbox-executor-metrics-test", litebus::AID(), "/tmp/sandbox-executor-test-ckpt");
    const auto info = BuildRuntimeInstanceInfo();

    executor.stateManager_.Register(SandboxInfo{info.runtimeid(), "sandbox-1", {}, {}, info});
    executor.DoReportMetrics(info.instanceid(), info.runtimeid(), "sandbox-1",
                             {"yr_app_instance_start_time", "start timestamp", "ms"});

    EXPECT_TRUE(metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.find("yr_app_instance_start_time") !=
                metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.end());
    EXPECT_TRUE(metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.find("yr_sandbox_lifecycle_status") !=
                metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.end());
    EXPECT_TRUE(metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.find("yr_sandbox_requested_cpu_cores") !=
                metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.end());
    EXPECT_TRUE(metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.find("yr_sandbox_requested_memory_bytes") !=
                metrics::MetricsAdapter::GetInstance().doubleGaugeMap_.end());
}

}  // namespace functionsystem::test
