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

#ifndef UT_MOCKS_MOCK_ETCD_KV_SERVICE_H
#define UT_MOCKS_MOCK_ETCD_KV_SERVICE_H

#include <gmock/gmock.h>

#include "meta_store_client/txn_transaction.h"

namespace functionsystem::test {
class MockTxnTransaction : public meta_store::TxnTransaction { // NOLINT
public:
    explicit MockTxnTransaction(const litebus::AID &actorAid): TxnTransaction(actorAid)
    {
    }

    ~MockTxnTransaction() = default;

    MOCK_METHOD(litebus::Future<std::shared_ptr<TxnResponse>>, Commit, (), (const, override));
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_ETCD_LEASE_SERVICE_H
