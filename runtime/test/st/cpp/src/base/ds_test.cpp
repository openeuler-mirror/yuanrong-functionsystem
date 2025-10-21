/*
* Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
*/
#include <stdlib.h>
#include <string>

#include "base/utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "user_common_func.h"
#include "yr/yr.h"

class DsTest : public testing::Test {
public:
   DsTest(){};
   ~DsTest(){};
   static void SetUpTestCase(){};
   static void TearDownTestCase(){};
   void SetUp()
   {
       YR::Config config;
       config.mode = YR::Config::Mode::CLUSTER_MODE;
       auto info = YR::Init(config);
       std::cout << "job id: " << info.jobId << std::endl;
   };
   void TearDown()
   {
       YR::Finalize();
   };
};

TEST_F(DsTest, KVWithTenantId)
{
   // set with valid tenantId
   YR::SetParam setParam;
   setParam.traceId = "executor-0";
   auto key = "cppkey1";
   auto value = "value1";
   YR::KV().Set(key, value, setParam);
   auto get = YR::KV().Get(key);
   ASSERT_EQ(value, get);

   // del with invalid tenantId
   YR::DelParam delParam;
   delParam.traceId = "1234$5678";
   ASSERT_THROW(YR::KV().Del(key, delParam), YR::Exception);

   // del with valid tenantId
   delParam.traceId = "executor-1";
   ASSERT_NO_THROW(YR::KV().Del(key, delParam));

   // set with invalid tenantId
   setParam.traceId = "1234$5678";
   ASSERT_THROW(YR::KV().Set(key, value, setParam), YR::Exception);
}
