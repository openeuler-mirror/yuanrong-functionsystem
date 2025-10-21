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

#pragma once
#include <gmock/gmock.h>
#include <cstdlib>
#include <msgpack.hpp>
#include <string>

#include "src/libruntime/heterostore/hetero_store.h"
#include "src/libruntime/objectstore/object_store.h"
#include "src/libruntime/statestore/state_store.h"

namespace YR {
namespace Libruntime {
class MockObjectStore : public ObjectStore {
public:
    MOCK_METHOD(ErrorInfo, Init, (const std::string &addr, int port, std::int32_t connectTimeout), (override));
    MOCK_METHOD(ErrorInfo, Init,
                (const std::string &addr, int port, bool enableDsAuth, bool encryptEnable,
                 const std::string &runtimePublicKey, const datasystem::SensitiveValue &runtimePrivateKey,
                 const std::string &dsPublicKey, std::int32_t connectTimeout),(override));
    MOCK_METHOD1(Init, ErrorInfo(datasystem::ConnectOptions &options));
    MOCK_METHOD(ErrorInfo, Put,
                (std::shared_ptr<Buffer> data, const std::string &objID,
                 const std::unordered_set<std::string> &nestedID, const CreateParam &createParam),
                (override));
    MOCK_METHOD(SingleResult, Get, (const std::string &objID, int timeoutMS), (override));
    MOCK_METHOD(MultipleResult, Get, (const std::vector<std::string> &ids, int timeoutMS), (override));
    MOCK_METHOD(ErrorInfo, IncreGlobalReference, (const std::vector<std::string> &objectIds), (override));
    MOCK_METHOD((std::pair<ErrorInfo, std::vector<std::string>>), IncreGlobalReference,
                (const std::vector<std::string> &objectIds, const std::string &remoteId), (override));
    MOCK_METHOD(ErrorInfo, DecreGlobalReference, (const std::vector<std::string> &objectIds), (override));
    MOCK_METHOD((std::pair<ErrorInfo, std::vector<std::string>>), DecreGlobalReference,
                (const std::vector<std::string> &objectIds, const std::string &remoteId), (override));
    MOCK_METHOD(std::vector<int>, QueryGlobalReference, (const std::vector<std::string> &objectIds), (override));
    MOCK_METHOD(ErrorInfo, GenerateKey, (std::string & key, const std::string &prefix, bool isPut), (override));
    MOCK_METHOD(ErrorInfo, CreateBuffer,
                (const std::string &objectId, size_t dataSize, std::shared_ptr<Buffer> &dataBuf,
                 const CreateParam &createParam),
                (override));
    MOCK_METHOD((std::pair<ErrorInfo, std::vector<std::shared_ptr<Buffer>>>), GetBuffers,
                (const std::vector<std::string> &ids, int timeoutMS), (override));
    MOCK_METHOD((std::pair<RetryInfo, std::vector<std::shared_ptr<Buffer>>>), GetBuffersWithoutRetry,
                (const std::vector<std::string> &ids, int timeoutMS), (override));
    MOCK_METHOD(void, SetTenantId, (const std::string &tenantId), (override));
    MOCK_METHOD(void, Clear, (), (override));
    MOCK_METHOD(void, Shutdown, (), (override));
};

class MockStateStore : public StateStore {
public:
    MockStateStore() : StateStore() {}
    MOCK_METHOD3(Init, ErrorInfo(const std::string &ip, int port, std::int32_t connectTimeout));
    MOCK_METHOD(ErrorInfo, Init,
                (const std::string &ip, int port, bool enableDsAuth, bool encryptEnable,
                 const std::string &runtimePublicKey, const datasystem::SensitiveValue &runtimePrivateKey,
                 const std::string &dsPublicKey, std::int32_t connectTimeout),
                (override));
    MOCK_METHOD1(Init, ErrorInfo(datasystem::ConnectOptions &options));
    MOCK_METHOD1(Init, ErrorInfo(const DsConnectOptions &options));
    MOCK_METHOD3(Write, ErrorInfo(const std::string &key, std::shared_ptr<Buffer> value, SetParam setParam));
    MOCK_METHOD3(Write, ErrorInfo(std::shared_ptr<Buffer> value, SetParam setParam, std::string &returnKey));
    MOCK_METHOD3(MSetTx, ErrorInfo(const std::vector<std::string> &keys,
                                   const std::vector<std::shared_ptr<Buffer>> &vals, const MSetParam &mSetParam));
    MOCK_METHOD2(Read, SingleReadResult(const std::string &key, int timeoutMS));
    MOCK_METHOD3(Read, MultipleReadResult(const std::vector<std::string> &keys, int timeoutMS, bool allowPartial));
    MOCK_METHOD3(GetWithParam,
                 MultipleReadResult(const std::vector<std::string> &keys, const GetParams &params, int timeout));
    MOCK_METHOD1(Del, ErrorInfo(const std::string &key));
    MOCK_METHOD1(Del, MultipleDelResult(const std::vector<std::string> &keys));
    MOCK_METHOD0(Shutdown, void());
    MOCK_METHOD1(GenerateKey, ErrorInfo(std::string &returnKey));
};

class MockHeretoStore : public HeteroStore {
public:
    MOCK_METHOD1(Init, ErrorInfo(datasystem::ConnectOptions & options));
    MOCK_METHOD0(Shutdown, void());
    MOCK_METHOD2(Delete,
                 ErrorInfo(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds));
    MOCK_METHOD2(LocalDelete,
                 ErrorInfo(const std::vector<std::string> &objectIds, std::vector<std::string> &failedObjectIds));
    MOCK_METHOD3(DevSubscribe,
                 ErrorInfo(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                           std::vector<std::shared_ptr<YR::Libruntime::HeteroFuture>> &futureVec));
    MOCK_METHOD3(DevPublish,
                 ErrorInfo(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                           std::vector<std::shared_ptr<YR::Libruntime::HeteroFuture>> &futureVec));
    MOCK_METHOD3(DevMSet, ErrorInfo(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                    std::vector<std::string> &failedKeys));
    MOCK_METHOD4(DevMGet, ErrorInfo(const std::vector<std::string> &keys, const std::vector<DeviceBlobList> &blob2dList,
                                    std::vector<std::string> &failedKeys, int32_t timeoutMs));
};
}  // namespace Libruntime
}  // namespace YR
