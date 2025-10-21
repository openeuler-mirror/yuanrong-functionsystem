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

#ifndef __LITEBUS_FAILURE_HPP__
#define __LITEBUS_FAILURE_HPP__

#include "async/status.hpp"

namespace litebus {

class Failure : public Status {
public:
    Failure() : Status(Status::KOK), errorCode(Status::KOK)
    {
    }

    Failure(int32_t code) : Status(code), errorCode(code)
    {
    }

    ~Failure() override
    {
    }

    int32_t GetErrorCode() const
    {
        return errorCode;
    }

private:
    Status::Code errorCode;
};

}    // namespace litebus

#endif /* __FAILURE_HPP__ */
