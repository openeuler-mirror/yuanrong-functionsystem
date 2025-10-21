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

#include "function_proxy/common/rate_limiter/token_bucket_rate_limiter.h"

#include "gtest/gtest.h"

namespace functionsystem::test {

using namespace testing;
using namespace functionsystem;

class TokenBucketRateLimiterTest : public testing::Test {
protected:
    void SetUp() override
    {
        limiter = std::make_shared<TokenBucketRateLimiter>(10UL, 1.0);
    }

    std::shared_ptr<TokenBucketRateLimiter> limiter;
};

TEST_F(TokenBucketRateLimiterTest, TryAcquire)
{
   // Test scenario 1: If there are tokens in the token bucket, attempting to acquire a token should be successful.
   ASSERT_TRUE(limiter->TryAcquire());

   // Test scenario 2: If there are no tokens in the token bucket, attempting to acquire a token should fail.
   for (int i = 0; i < 10; i++) {
       limiter->TryAcquire();
   }
   ASSERT_FALSE(limiter->TryAcquire());

   sleep(2);
   ASSERT_TRUE(limiter->TryAcquire());
}

}