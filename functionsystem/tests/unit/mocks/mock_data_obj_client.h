/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef UT_MOCKS_MOCK_DATA_OBJ_CLIENT_H
#define UT_MOCKS_MOCK_DATA_OBJ_CLIENT_H

#include <gmock/gmock.h>

#include "function_proxy/common/data_obj_client/data_obj_client.h"

namespace functionsystem::test {

class MockDataObjClient : public DataObjClient {
public:
    MockDataObjClient() : DataObjClient(nullptr)
    {
    }
    MOCK_METHOD(void, InitDistributedCacheClient, (), (override));
    MOCK_METHOD(litebus::Future<std::vector<std::string>>, GetObjNodeList,
                (const std::string &tenantId, const std::vector<std::string> &objIDs), (override));
};

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_DATA_OBJ_CLIENT_H
