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
#include <string>
#include <thread>
#include <vector>

#include "common/types/instance_state.h"
#include "function_master/global_scheduler/traefik_route_cache.h"
#include "nlohmann/json.hpp"

namespace functionsystem::test {

using namespace functionsystem::global_scheduler;

// Helper to build a mock InstanceInfo with portForward extension
static resource_view::InstanceInfo MakeInstance(
    const std::string& instanceID,
    const std::string& proxyGrpcAddress,
    const std::string& portForwardJson,
    int32_t statusCode)
{
    resource_view::InstanceInfo info;
    info.set_instanceid(instanceID);
    info.set_proxygrpcaddress(proxyGrpcAddress);
    if (!portForwardJson.empty()) {
        (*info.mutable_extensions())["portForward"] = portForwardJson;
    }
    info.mutable_instancestatus()->set_code(statusCode);
    return info;
}

class TraefikRouteCacheTest : public ::testing::Test {
protected:
    TraefikConfig cfg_;
    std::shared_ptr<TraefikRouteCache> cache_;

    void SetUp() override
    {
        cfg_.httpEntryPoint   = "websecure";
        cfg_.enableTLS        = true;
        cfg_.serversTransport = "yr-backend-tls@file";
        cache_ = std::make_shared<TraefikRouteCache>(cfg_);
    }
};

TEST_F(TraefikRouteCacheTest, EmptyCache_ReturnsValidEmptyConfig)
{
    std::string json = cache_->GetConfigJSON();
    ASSERT_FALSE(json.empty());

    auto parsed = nlohmann::json::parse(json);
    EXPECT_TRUE(parsed.contains("http"));
    EXPECT_TRUE(parsed["http"].contains("middlewares"));
    // When route table is empty, "routers" and "services" keys are intentionally omitted.
    // Traefik's paerser cannot decode empty JSON objects ({}) into map types
    // ("cannot be a standalone element"), so omitting the key is the correct
    // Traefik-compatible representation of "no routes".
    EXPECT_FALSE(parsed["http"].contains("routers"));
    EXPECT_FALSE(parsed["http"].contains("services"));
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_NewFormatPort)
{
    // New format: "protocol:hostPort:containerPort"
    auto instance = MakeInstance(
        "inst-001", "10.0.0.1:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    EXPECT_TRUE(parsed["http"]["routers"].contains("inst-001-p8080"));
    auto& router = parsed["http"]["routers"]["inst-001-p8080"];
    EXPECT_EQ(router["rule"], "PathPrefix(`/inst-001/8080`)");
    EXPECT_EQ(router["service"], "inst-001-p8080");
    EXPECT_TRUE(router.contains("tls"));

    auto& service = parsed["http"]["services"]["inst-001-p8080"];
    EXPECT_EQ(service["loadBalancer"]["servers"][0]["url"], "https://10.0.0.1:40001");
    EXPECT_EQ(service["loadBalancer"]["serversTransport"], "yr-backend-tls@file");
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_LegacyFormatPort)
{
    // Legacy format: "hostPort:containerPort" (no protocol, defaults to http)
    auto instance = MakeInstance(
        "inst-002", "10.0.0.2:50000",
        R"(["40002:9090"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    auto& service = parsed["http"]["services"]["inst-002-p9090"];
    EXPECT_EQ(service["loadBalancer"]["servers"][0]["url"], "http://10.0.0.2:40002");
    // HTTP backend should NOT have serversTransport
    EXPECT_FALSE(service["loadBalancer"].contains("serversTransport"));
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_MultiplePorts)
{
    auto instance = MakeInstance(
        "inst-003", "10.0.0.3:50000",
        R"(["https:40001:8080", "http:40002:9090"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 2);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    EXPECT_TRUE(parsed["http"]["routers"].contains("inst-003-p8080"));
    EXPECT_TRUE(parsed["http"]["routers"].contains("inst-003-p9090"));
}

TEST_F(TraefikRouteCacheTest, OnInstanceExited_RemovesRoutes)
{
    auto instance = MakeInstance(
        "inst-004", "10.0.0.4:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    cache_->OnInstanceExited("inst-004");
    EXPECT_EQ(cache_->GetRouteCount(), 0);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);
    // When route table is empty, "routers" and "services" keys are intentionally omitted.
    // Traefik's paerser cannot decode {} into map types — omitting is correct.
    EXPECT_FALSE(parsed["http"].contains("routers"));
    EXPECT_FALSE(parsed["http"].contains("services"));
}

TEST_F(TraefikRouteCacheTest, OnInstanceExited_NonexistentID_IsNoop)
{
    cache_->OnInstanceExited("nonexistent");
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_NoPortForward_IsNoop)
{
    // No portForward extension
    auto instance = MakeInstance(
        "inst-005", "10.0.0.5:50000", "",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_InvalidProxyAddress_IsNoop)
{
    auto instance = MakeInstance(
        "inst-006", "invalid-address",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, SanitizeID_SpecialCharacters)
{
    auto instance = MakeInstance(
        "tenant@user/func.v1_inst", "10.0.0.1:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    // @ → -at-, / → -, . → -, _ → -
    EXPECT_TRUE(parsed["http"]["routers"].contains("tenant-at-user-func-v1-inst-p8080"));
}

TEST_F(TraefikRouteCacheTest, OnInstanceRunning_UpdateExistingInstance)
{
    // First registration
    auto instance = MakeInstance(
        "inst-007", "10.0.0.7:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    // Update with different ports
    auto updated = MakeInstance(
        "inst-007", "10.0.0.7:50000",
        R"(["https:40002:9090", "http:40003:3000"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(updated);
    EXPECT_EQ(cache_->GetRouteCount(), 2);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);
    EXPECT_FALSE(parsed["http"]["routers"].contains("inst-007-p8080"));
    EXPECT_TRUE(parsed["http"]["routers"].contains("inst-007-p9090"));
    EXPECT_TRUE(parsed["http"]["routers"].contains("inst-007-p3000"));
}

TEST_F(TraefikRouteCacheTest, GetConfigJSON_CachesResult)
{
    auto instance = MakeInstance(
        "inst-008", "10.0.0.8:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);

    // First call builds cache
    std::string json1 = cache_->GetConfigJSON();
    // Second call should return identical bytes (from cache)
    std::string json2 = cache_->GetConfigJSON();

    EXPECT_EQ(json1, json2);
}

TEST_F(TraefikRouteCacheTest, GetConfigJSON_DirtyAfterChange)
{
    auto instance = MakeInstance(
        "inst-009", "10.0.0.9:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    std::string json1 = cache_->GetConfigJSON();

    // Add another instance
    auto instance2 = MakeInstance(
        "inst-010", "10.0.0.10:50000",
        R"(["https:40002:9090"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance2);
    std::string json2 = cache_->GetConfigJSON();

    EXPECT_NE(json1, json2);
}

TEST_F(TraefikRouteCacheTest, NoTLS_RouterHasNoTlsField)
{
    cfg_.enableTLS = false;
    cache_ = std::make_shared<TraefikRouteCache>(cfg_);

    auto instance = MakeInstance(
        "inst-011", "10.0.0.11:50000",
        R"(["http:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    EXPECT_FALSE(parsed["http"]["routers"]["inst-011-p8080"].contains("tls"));
}

TEST_F(TraefikRouteCacheTest, MultipleInstances_SortedKeys)
{
    // Add instances in reverse order
    auto inst3 = MakeInstance("z-inst", "10.0.0.3:50000", R"(["https:40003:8080"])",
                              static_cast<int32_t>(InstanceState::RUNNING));
    auto inst1 = MakeInstance("a-inst", "10.0.0.1:50000", R"(["https:40001:8080"])",
                              static_cast<int32_t>(InstanceState::RUNNING));
    auto inst2 = MakeInstance("m-inst", "10.0.0.2:50000", R"(["https:40002:8080"])",
                              static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(inst3);
    cache_->OnInstanceRunning(inst1);
    cache_->OnInstanceRunning(inst2);

    std::string json = cache_->GetConfigJSON();

    // Verify keys are sorted (a-inst < m-inst < z-inst)
    size_t posA = json.find("a-inst-p8080");
    size_t posM = json.find("m-inst-p8080");
    size_t posZ = json.find("z-inst-p8080");

    EXPECT_LT(posA, posM);
    EXPECT_LT(posM, posZ);
}

TEST_F(TraefikRouteCacheTest, StripPrefixMiddleware_Present)
{
    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    EXPECT_TRUE(parsed["http"]["middlewares"].contains("stripprefix-all"));
    EXPECT_EQ(parsed["http"]["middlewares"]["stripprefix-all"]["stripPrefixRegex"]["regex"][0],
              "^/[^/]+/[0-9]+");
}

TEST_F(TraefikRouteCacheTest, InstanceLifecycle_RunningThenFatalThenDelete)
{
    // Simulate full lifecycle: RUNNING → add route, FATAL → remove route, DELETE → no-op
    auto running = MakeInstance("inst-lc", "10.0.0.1:50000",
                                R"(["https:40001:8080"])",
                                static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(running);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    // Instance transitions to FATAL (via OnInstancePut hook)
    cache_->OnInstanceExited("inst-lc");
    EXPECT_EQ(cache_->GetRouteCount(), 0);

    // Delete event for same instance is a no-op
    cache_->OnInstanceExited("inst-lc");
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, InvalidPortForward_MalformedJSON)
{
    auto instance = MakeInstance(
        "inst-bad-json", "10.0.0.1:50000",
        "not-valid-json",
        static_cast<int32_t>(InstanceState::RUNNING));

    // Should not crash, just skip
    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, InvalidPortForward_WrongArrayType)
{
    auto instance = MakeInstance(
        "inst-wrong-type", "10.0.0.1:50000",
        R"([123, 456])",
        static_cast<int32_t>(InstanceState::RUNNING));

    // Non-string array entries should be skipped
    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, InvalidPortForward_BadPartCount)
{
    auto instance = MakeInstance(
        "inst-bad-parts", "10.0.0.1:50000",
        R"(["a:b:c:d"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    // 4-part mapping is invalid
    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

TEST_F(TraefikRouteCacheTest, EntryPointConfig_Customized)
{
    cfg_.httpEntryPoint = "custom-ep";
    cfg_.enableTLS = true;
    cache_ = std::make_shared<TraefikRouteCache>(cfg_);

    auto instance = MakeInstance(
        "inst-ep", "10.0.0.1:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(instance);

    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
    EXPECT_EQ(parsed["http"]["routers"]["inst-ep-p8080"]["entryPoints"][0], "custom-ep");
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional edge-case and coverage tests
// ─────────────────────────────────────────────────────────────────────────────

// Empty portForward array [] should be a no-op (no routes added)
TEST_F(TraefikRouteCacheTest, OnInstanceRunning_EmptyPortForwardArray_IsNoop)
{
    auto instance = MakeInstance(
        "inst-empty-arr", "10.0.0.1:50000",
        R"([])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

// proxyGrpcAddress of the form ":50000" (no host part) → ExtractIP returns ""
// → ParseRoutes bails out → no route added
TEST_F(TraefikRouteCacheTest, OnInstanceRunning_NoHostInProxyAddress_IsNoop)
{
    auto instance = MakeInstance(
        "inst-nohost", ":50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 0);
}

// When cfg_.serversTransport is empty, HTTPS backends must NOT emit serversTransport key
TEST_F(TraefikRouteCacheTest, EmptyServersTransport_NotInJson)
{
    cfg_.serversTransport = "";
    cache_ = std::make_shared<TraefikRouteCache>(cfg_);

    auto instance = MakeInstance(
        "inst-no-st", "10.0.0.1:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
    EXPECT_FALSE(parsed["http"]["services"]["inst-no-st-p8080"]["loadBalancer"].contains("serversTransport"));
}

// Protocol matching is case-insensitive: "HTTPS" and "Http" should both work
TEST_F(TraefikRouteCacheTest, OnInstanceRunning_ProtocolCaseInsensitive)
{
    auto inst1 = MakeInstance(
        "inst-upper", "10.0.0.1:50000",
        R"(["HTTPS:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst1);

    auto inst2 = MakeInstance(
        "inst-mixed", "10.0.0.2:50000",
        R"(["Http:40002:9090"])",
        static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst2);

    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());

    // HTTPS → backendURL starts with https://
    EXPECT_EQ(parsed["http"]["services"]["inst-upper-p8080"]["loadBalancer"]["servers"][0]["url"],
              "https://10.0.0.1:40001");
    // Http → backendURL starts with http://
    EXPECT_EQ(parsed["http"]["services"]["inst-mixed-p9090"]["loadBalancer"]["servers"][0]["url"],
              "http://10.0.0.2:40002");
}

// Instance ID longer than 200 chars must be truncated in the router/service name
TEST_F(TraefikRouteCacheTest, LongInstanceID_TruncatedInRouterName)
{
    std::string longID(250, 'a');  // 250-char ID
    auto instance = MakeInstance(
        longID, "10.0.0.1:50000",
        R"(["https:40001:8080"])",
        static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(instance);
    EXPECT_EQ(cache_->GetRouteCount(), 1);

    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    // router name = truncated(safeID, 200) + "-p8080"
    // safeID is 250 'a' chars (no special chars), truncated to 200
    std::string expectedName = std::string(200, 'a') + "-p8080";
    EXPECT_TRUE(parsed["http"]["routers"].contains(expectedName));
}

// GetRouteCount sums port entries across all registered instances
TEST_F(TraefikRouteCacheTest, GetRouteCount_SumsAcrossMultipleInstances)
{
    auto inst1 = MakeInstance("inst-A", "10.0.0.1:50000",
                              R"(["https:40001:8080","https:40002:9090"])",
                              static_cast<int32_t>(InstanceState::RUNNING));
    auto inst2 = MakeInstance("inst-B", "10.0.0.2:50000",
                              R"(["https:40003:3000"])",
                              static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(inst1);
    cache_->OnInstanceRunning(inst2);

    // inst-A contributes 2 ports, inst-B contributes 1 → total 3
    EXPECT_EQ(cache_->GetRouteCount(), 3);

    cache_->OnInstanceExited("inst-A");
    EXPECT_EQ(cache_->GetRouteCount(), 1);
}

// Concurrent calls to OnInstanceRunning / OnInstanceExited must not crash
// or produce a corrupted JSON (data-race check via thread sanitizer)
TEST_F(TraefikRouteCacheTest, ConcurrentAccess_NoDataRace)
{
    constexpr int kInstances = 20;
    constexpr int kThreads   = 4;

    // Pre-build instance list
    std::vector<resource_view::InstanceInfo> instances;
    instances.reserve(kInstances);
    for (int i = 0; i < kInstances; ++i) {
        instances.push_back(MakeInstance(
            "inst-concurrent-" + std::to_string(i),
            "10.0." + std::to_string(i / 256) + "." + std::to_string(i % 256) + ":50000",
            R"(["https:40001:8080"])",
            static_cast<int32_t>(InstanceState::RUNNING)));
    }

    // Writers: add instances
    auto writer = [&]() {
        for (const auto& inst : instances) {
            cache_->OnInstanceRunning(inst);
        }
    };
    // Readers: poll JSON
    auto reader = [&]() {
        for (int i = 0; i < kInstances; ++i) {
            (void)cache_->GetConfigJSON();
        }
    };
    // Deleters: remove instances
    auto deleter = [&]() {
        for (const auto& inst : instances) {
            cache_->OnInstanceExited(inst.instanceid());
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(writer);
        threads.emplace_back(reader);
    }
    threads.emplace_back(deleter);

    for (auto& th : threads) {
        th.join();
    }

    // After all threads finish, JSON must still be valid
    std::string json = cache_->GetConfigJSON();
    EXPECT_NO_THROW(nlohmann::json::parse(json));
}

// After adding then removing all instances, JSON is byte-identical to initial empty config
TEST_F(TraefikRouteCacheTest, AfterAllInstancesRemoved_JSONEqualsInitialEmpty)
{
    std::string emptyJson = cache_->GetConfigJSON();

    auto inst = MakeInstance("inst-tmp", "10.0.0.1:50000",
                             R"(["https:40001:8080"])",
                             static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst);
    cache_->OnInstanceExited("inst-tmp");

    std::string restoredJson = cache_->GetConfigJSON();
    EXPECT_EQ(emptyJson, restoredJson);
}

// ─────────────────────────────────────────────────────────────────────────────
// Traefik-compatibility regression tests
//
// These tests verify that GetConfigJSON() always produces JSON that can be
// decoded by Traefik's paerser library.  Traefik's file.DecodeContent (used
// internally by the HTTP provider) cannot decode empty JSON objects ({}) into
// Go map types, returning "cannot be a standalone element".
// The fix: omit "routers" and "services" keys entirely when the route table
// is empty, which is the idiomatic Traefik dynamic-config representation for
// "no routes".
// ─────────────────────────────────────────────────────────────────────────────

// [Regression] Empty cache must NOT emit "routers"/"services" keys.
// Before the fix, BuildConfigJSON always wrote "routers": {} which caused
// Traefik to error "cannot decode: routers cannot be a standalone element".
TEST_F(TraefikRouteCacheTest, TraefikCompat_EmptyCache_NoRoutersServicesKeys)
{
    std::string json = cache_->GetConfigJSON();
    auto parsed = nlohmann::json::parse(json);

    // Traefik can decode {} only when the key is absent — not when it is an empty map
    EXPECT_FALSE(parsed["http"].contains("routers"))
        << "Empty cache must not emit 'routers: {}' (Traefik paerser incompatibility)";
    EXPECT_FALSE(parsed["http"].contains("services"))
        << "Empty cache must not emit 'services: {}' (Traefik paerser incompatibility)";
    // Middlewares are always emitted (Traefik handles empty middleware objects fine)
    EXPECT_TRUE(parsed["http"].contains("middlewares"));
}

// [Regression] After removing the last instance, the config must revert to the
// Traefik-compatible empty format (no "routers"/"services" keys).
// Before the fix this case was broken: Traefik kept the stale route because
// every attempt to decode the empty-map JSON failed, preventing route removal.
TEST_F(TraefikRouteCacheTest, TraefikCompat_AddThenRemoveAll_Reverts)
{
    auto inst = MakeInstance("inst-revert", "10.0.0.1:50000",
                             R"(["https:40001:8080"])",
                             static_cast<int32_t>(InstanceState::RUNNING));

    cache_->OnInstanceRunning(inst);
    {
        auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
        ASSERT_TRUE(parsed["http"].contains("routers")) << "Route should be present after add";
    }

    cache_->OnInstanceExited("inst-revert");
    EXPECT_EQ(cache_->GetRouteCount(), 0);
    {
        auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
        EXPECT_FALSE(parsed["http"].contains("routers"))
            << "After removing last route, 'routers' key must be absent (Traefik-compat)";
        EXPECT_FALSE(parsed["http"].contains("services"))
            << "After removing last route, 'services' key must be absent (Traefik-compat)";
    }
}

// [Guard] When routes ARE present, "routers" and "services" keys must be emitted.
// This test guards against accidentally over-applying the empty-map omission.
TEST_F(TraefikRouteCacheTest, TraefikCompat_NonEmpty_ContainsRoutersAndServices)
{
    auto inst = MakeInstance("inst-guard", "10.0.0.1:50000",
                             R"(["https:40001:8080"])",
                             static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst);

    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
    EXPECT_TRUE(parsed["http"].contains("routers"))
        << "Non-empty cache must emit 'routers' key";
    EXPECT_TRUE(parsed["http"].contains("services"))
        << "Non-empty cache must emit 'services' key";
}

// [Traefik format] Verify the full expected structure of a single-route config
// matches the format Traefik's HTTP provider expects: entryPoints, rule, service,
// middleware ref, loadBalancer.servers[].url.
TEST_F(TraefikRouteCacheTest, TraefikCompat_SingleRoute_FullStructureValid)
{
    auto inst = MakeInstance("my-func", "192.168.1.5:50000",
                             R"(["https:40080:8080"])",
                             static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst);

    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
    const std::string routerName = "my-func-p8080";

    // Router structure
    ASSERT_TRUE(parsed["http"]["routers"].contains(routerName));
    const auto& router = parsed["http"]["routers"][routerName];
    EXPECT_EQ(router["rule"], "PathPrefix(`/my-func/8080`)");
    EXPECT_EQ(router["service"], routerName);
    EXPECT_EQ(router["entryPoints"][0], "websecure");
    EXPECT_EQ(router["middlewares"][0], "stripprefix-all");
    EXPECT_TRUE(router.contains("tls"));         // cfg_.enableTLS defaults to true

    // Service / loadBalancer structure
    ASSERT_TRUE(parsed["http"]["services"].contains(routerName));
    const auto& lb = parsed["http"]["services"][routerName]["loadBalancer"];
    EXPECT_EQ(lb["servers"][0]["url"], "https://192.168.1.5:40080");
    EXPECT_EQ(lb["serversTransport"], "yr-backend-tls@file");

    // Middleware definition
    EXPECT_EQ(parsed["http"]["middlewares"]["stripprefix-all"]
              ["stripPrefixRegex"]["regex"][0], "^/[^/]+/[0-9]+");
}

// [Traefik format] http-protocol backend must NOT emit serversTransport and no tls field.
TEST_F(TraefikRouteCacheTest, TraefikCompat_HttpBackend_NoTlsNoTransport)
{
    cfg_.enableTLS = false;
    cache_ = std::make_shared<TraefikRouteCache>(cfg_);

    auto inst = MakeInstance("plain-func", "10.0.0.1:50000",
                             R"(["http:40080:8080"])",
                             static_cast<int32_t>(InstanceState::RUNNING));
    cache_->OnInstanceRunning(inst);

    auto parsed = nlohmann::json::parse(cache_->GetConfigJSON());
    const auto& router = parsed["http"]["routers"]["plain-func-p8080"];
    EXPECT_FALSE(router.contains("tls")) << "HTTP router must not have tls field";

    const auto& lb = parsed["http"]["services"]["plain-func-p8080"]["loadBalancer"];
    EXPECT_EQ(lb["servers"][0]["url"], "http://10.0.0.1:40080");
    EXPECT_FALSE(lb.contains("serversTransport")) << "HTTP backend must not have serversTransport";
}

}  // namespace functionsystem::test
