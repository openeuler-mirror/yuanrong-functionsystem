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

#ifndef FUNCTION_PROXY_COMMON_TOKEN_BUCKET_RATE_LIMITER_H
#define FUNCTION_PROXY_COMMON_TOKEN_BUCKET_RATE_LIMITER_H

#include "rate_limiter.h"

#include <chrono>

namespace functionsystem {
class TokenBucketRateLimiter : public RateLimiter {
public:
    TokenBucketRateLimiter()
    {
        lastRefillTime_ = std::chrono::high_resolution_clock::now();
    }
    explicit TokenBucketRateLimiter(uint64_t capacity, float refillRate)
        : capacity_(capacity), refillRate_(refillRate), tokens_(capacity)
    {
        lastRefillTime_ = std::chrono::high_resolution_clock::now();
    }
    ~TokenBucketRateLimiter() override
    {}

    bool TryAcquire() override;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastRefillTime_;  // last token refill time
private:
    uint64_t capacity_{0};                                                        // token bucket capacity
    float refillRate_;                                                            // token refill rate
    uint64_t tokens_;                                                             // current token count
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_TOKEN_BUCKET_RATE_LIMITER_H
