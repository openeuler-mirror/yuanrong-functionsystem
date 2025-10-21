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

#include <exec/exec.hpp>
#include <fstream>
#include <timer/timertools.hpp>
#include <utils/os_utils.hpp>

#include "common/constants/actor_name.h"
#include "heartbeat/ping_pong_driver.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/resource_view/view_utils.h"
#include "common/scheduler_topology/sched_tree.h"
#include "stubs/etcd_service/etcd_service_driver.h"
#include "utils.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {
const std::string NODE_ID = "it_function_proxy";  // NOLINT
const std::string PROCESS_IP = "127.0.0.1:5656";  // NOLINT
const std::string SYS_FUNC_CUSTOM_ARGS = "";      // NOLINT

const std::string SYSTEM_FUNC_CONFIG_PATH = "/home/sn/function/config";     // NOLINT
const std::string SYSTEM_FUNC_CONFIG_FILE = "system-function-config.json";  // NOLINT

const std::string DEFAULT_ELECTION_MODE = "standalone";  // NOLINE
const std::string DEFAULT_SCHEDULE_PLUGINS =
    "[\"Label\", \"ResourceSelector\", \"Default\", \"Heterogeneous\"]";  // NOLINE

class LocalSchedulerMockActor : public litebus::ActorBase {
public:
    explicit LocalSchedulerMockActor(const std::string &name) : litebus::ActorBase(name)
    {
        globalActorAid_.SetProtocol(litebus::BUS_TCP);
        globalActorAid_.SetName(LOCAL_SCHED_MGR_ACTOR_NAME);
        globalActorAid_.SetUrl(PROCESS_IP);
    }

    ~LocalSchedulerMockActor() override = default;

    litebus::Future<Status> RegisterToGlobal()
    {
        YRLOG_ERROR("register to global_scheduler");

        messages::Register reg;
        reg.set_name(NODE_ID);
        reg.set_address(GetAID().UnfixUrl());

        *reg.mutable_resource() = view_utils::Get1DResourceUnit(NODE_ID);

        Send(globalActorAid_, "Register", reg.SerializeAsString());

        return registered_.GetFuture();
    }

    litebus::Future<Status> RegisterToDomain()
    {
        YRLOG_INFO("register to domain_scheduler");

        messages::Register reg;
        reg.set_name(NODE_ID);
        reg.set_address(GetAID().UnfixUrl());

        resource_view::ResourceUnit localView = view_utils::Get1DResourceUnit(NODE_ID);
        resource_view::ResourceUnit agentUnit = view_utils::Get1DResourceUnit("test-agent");
        agentUnit.set_ownerid(NODE_ID);
        auto fragment = localView.mutable_fragment();
        (*fragment)[agentUnit.id()] = agentUnit;
        (*reg.mutable_resources())[0] = localView;
        (*reg.mutable_resources())[1] = view_utils::Get1DResourceUnit(NODE_ID);

        Send(domainActorAid_, "Register", reg.SerializeAsString());

        return Status::OK();
    }

    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::Registered registered;
        (void)registered.ParseFromString(msg);

        if (from.Name() == globalActorAid_.Name()) {
            // registered message from global scheduler
            OnRegisterToGlobal(registered);
        } else if (from.Name() == domainActorAid_.Name()) {
            // registered message from domain scheduler
            onRegisterToDomain(registered);
        } else {
            YRLOG_WARN("get unexpected name of: {}", from.Name());
        }
    }

    void Schedule(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        scheduleRequest_ = std::make_shared<messages::ScheduleRequest>();
        scheduleRequest_->ParseFromString(msg);
        YRLOG_INFO("receive a schedule request({})", scheduleRequest_->ShortDebugString());

        messages::ScheduleResponse response;
        response.set_code(StatusCode::SUCCESS);
        response.set_message("succeed to init runtime");
        response.set_requestid(scheduleRequest_->requestid());
        response.set_instanceid(scheduleRequest_->instance().instanceid());

        Send(from, "ResponseSchedule", std::move(response.SerializeAsString()));
    }

private:
    void OnRegisterToGlobal(const messages::Registered &registered)
    {
        if (registered.code() != int32_t(StatusCode::SUCCESS)) {
            YRLOG_ERROR("failed to register to global scheduler, errCode: {}, errMsg: {}", registered.code(),
                        registered.message());
        } else {
            auto leader = registered.topo().leader();
            YRLOG_INFO("succeed to register to global scheduler, obtain a domain scheduler(name: {}, address: {})",
                       leader.name(), leader.address());
            domainActorAid_.SetName(leader.name() + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
            domainActorAid_.SetUrl(leader.address());
            StartPingPong();

            std::this_thread::sleep_for(std::chrono::seconds(10));
            RegisterToDomain();
        }
    }

    void onRegisterToDomain(const messages::Registered &registered)
    {
        if (registered.code() != int32_t(StatusCode::SUCCESS)) {
            YRLOG_ERROR("failed to register to domain scheduler, errCode: {}, errMsg: {}", registered.code(),
                        registered.message());
        } else {
            YRLOG_INFO("succeed to register to domain scheduler({})", domainActorAid_.Name());
            registered_.SetValue(Status::OK());
        }
    }

    void StartPingPong()
    {
        if (pingPongDriver_ != nullptr) {
            YRLOG_INFO("ping pong server has started.");
            return;
        }
        YRLOG_INFO("start a ping pong receiving message from domain scheduler");
        pingPongDriver_ = std::make_shared<PingPongDriver>(
            NODE_ID, 5000, [aid(GetAID())](const litebus::AID &, HeartbeatConnection type) {
                YRLOG_ERROR("timeout to connect domain scheduler.");
            });
    }

protected:
    void Init() override
    {
        Receive("Registered", &LocalSchedulerMockActor::Registered);
        Receive("Schedule", &LocalSchedulerMockActor::Schedule);
    }

public:
    std::shared_ptr<PingPongDriver> pingPongDriver_;
    std::shared_ptr<messages::ScheduleRequest> scheduleRequest_;
    litebus::Promise<Status> registered_;

private:
    litebus::AID globalActorAid_, domainActorAid_;
};

class FunctionMasterTest : public ::testing::Test {
public:
    [[maybe_unused]] void SetUp() override
    {
        metaStoreServerPort_ = GetPortEnv("META_STORE_SERVER_PORT", 60000);
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        etcdSrvDriver_->StartServer("127.0.0.1:" + std::to_string(metaStoreServerPort_));

        auto binPathEnv = litebus::os::GetEnv("BIN_PATH");
        ASSERT_FALSE(binPathEnv.IsNone());
        binDir_ = binPathEnv.Get();

        if (!litebus::os::ExistPath(SYSTEM_FUNC_CONFIG_PATH)) {
            litebus::os::Mkdir(SYSTEM_FUNC_CONFIG_PATH);
        }
        StartFunctionMaster();
    }

    [[maybe_unused]] void TearDown() override
    {
        YRLOG_INFO("stop function_master process");
        KillProcess(process_.Get()->GetPid(), 2);

        etcdSrvDriver_->StopServer();

        DeleteSystemFunctionConfigFile();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    void StartFunctionMaster()
    {
        YRLOG_INFO("start function_master process");
        const std::string path = binDir_ + "/function_master";
        const std::vector<std::string> args = {
            "",
            "--node_id=it",  // do not modify
            "--ip=" + PROCESS_IP,
            "--meta_store_address=127.0.0.1:" + std::to_string(metaStoreServerPort_),
            "--sys_func_retry_period=5000",
            "--sys_func_custom_args=" + SYS_FUNC_CUSTOM_ARGS,
            R"(--log_config={"filepath": "/home/yr/log","level": "DEBUG","rolling": {"maxsize": )"
            R"(100,"maxfiles": 1}, "alsologtostderr": true})",
            "--election_mode=" + DEFAULT_ELECTION_MODE,
            "--schedule_plugins=" + DEFAULT_SCHEDULE_PLUGINS
        };

        process_ = CreateProcess(path, args);
        ASSERT_TRUE(process_.IsOK());
    }

    void WriteSystemFunctionConfigFile(const std::string &content)
    {
        if (!litebus::os::ExistPath(SYSTEM_FUNC_CONFIG_PATH)) {
            litebus::os::Mkdir(SYSTEM_FUNC_CONFIG_PATH);
        }
        auto filePath = SYSTEM_FUNC_CONFIG_PATH + "/" + SYSTEM_FUNC_CONFIG_FILE;

        std::ofstream outfile;
        outfile.open(filePath.c_str());
        outfile << content << std::endl;
        outfile.close();
    }

    void DeleteSystemFunctionConfigFile()
    {
        auto file = litebus::os::Join(SYSTEM_FUNC_CONFIG_PATH, SYSTEM_FUNC_CONFIG_FILE);
        if (litebus::os::ExistPath(file)) {
            litebus::os::Rm(file);
        }
    }

protected:
    std::string binDir_;
    uint16_t metaStoreServerPort_;
    litebus::Try<std::shared_ptr<litebus::Exec>> process_;  // NOLINT
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
};

TEST_F(FunctionMasterTest, StartTest)  // NOLINT
{
    auto actor = std::make_shared<LocalSchedulerMockActor>("LocalScheduler");
    litebus::AID aid = litebus::Spawn(actor);

    auto status = litebus::Async(aid, &LocalSchedulerMockActor::RegisterToGlobal).Get();
    EXPECT_EQ(status.IsOk(), true);
    litebus::Terminate(aid);
    litebus::Await(aid);
}
}  // namespace functionsystem::test
