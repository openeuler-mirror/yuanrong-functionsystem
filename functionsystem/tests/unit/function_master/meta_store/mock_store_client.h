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

#ifndef FUNCTIONSYSTEM_TEST_UNIT_MOCK_STORE_CLIENT_H
#define FUNCTIONSYSTEM_TEST_UNIT_MOCK_STORE_CLIENT_H

#include <gmock/gmock.h>
#include "actor/actor.hpp"
#include "etcd/api/etcdserverpb/rpc.pb.h"

namespace functionsystem::meta_store::test {

class MockMetaStoreClientActor : public litebus::ActorBase {
public:
    explicit MockMetaStoreClientActor(const std::string &name) : litebus::ActorBase(name)
    {
    }
    ~MockMetaStoreClientActor() override = default;

    void OnPut(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Put response from {}", from.HashString());
        MockOnPut(from, name, msg);
    }
    void OnDelete(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Delete response from {}", from.HashString());
        MockOnDelete(from, name, msg);
    }
    void OnGet(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Get response from {}", from.HashString());
        MockOnGet(from, name, msg);
    }
    void OnTxn(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Txn response from {}", from.HashString());
        MockOnTxn(from, name, msg);
    }
    void OnWatch(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Watch response from {}", from.HashString());
        MockOnWatch(from, name, msg);
    }
    void OnGetAndWatch(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received GetAndWatch response from {}", from.HashString());
        MockOnGetAndWatch(from, name, msg);
    }
    void GrantCallback(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Grant response from {}", from.HashString());
        MockGrantCallback(from, name, msg);
    }
    void RevokeCallback(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received Revoke response from {}", from.HashString());
        MockRevokeCallback(from, name, msg);
    }
    void KeepAliveOnceCallback(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        YRLOG_INFO("received KeepAliveOnce response from {}", from.HashString());
        MockKeepAliveOnceCallback(from, name, msg);
    }

    MOCK_METHOD(void, MockOnPut, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockOnDelete, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockOnGet, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockOnTxn, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockOnWatch, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockOnGetAndWatch, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockGrantCallback, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockRevokeCallback, (const litebus::AID &, std::string, std::string));
    MOCK_METHOD(void, MockKeepAliveOnceCallback, (const litebus::AID &, std::string, std::string));

protected:
    void Init() override
    {
        Receive("OnPut", &MockMetaStoreClientActor::OnPut);
        Receive("OnDelete", &MockMetaStoreClientActor::OnDelete);
        Receive("OnGet", &MockMetaStoreClientActor::OnGet);
        Receive("OnTxn", &MockMetaStoreClientActor::OnTxn);
        Receive("OnWatch", &MockMetaStoreClientActor::OnWatch);
        Receive("OnGetAndWatch", &MockMetaStoreClientActor::OnGetAndWatch);
        Receive("GrantCallback", &MockMetaStoreClientActor::GrantCallback);
        Receive("RevokeCallback", &MockMetaStoreClientActor::RevokeCallback);
        Receive("KeepAliveCallback", &MockMetaStoreClientActor::KeepAliveOnceCallback);
    }
    void Finalize() override {}
};

}  // namespace functionsystem::test

#endif