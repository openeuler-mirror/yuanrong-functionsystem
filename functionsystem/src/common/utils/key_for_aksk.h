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


#ifndef COMMON_KEY_FOR_AKSK_H
#define COMMON_KEY_FOR_AKSK_H

#include <string>
#include "common/utils/sensitive_value.h"

namespace functionsystem {

class KeyForAKSK {
public:
    KeyForAKSK()
    = default;

    KeyForAKSK(std::string accessKeyId, SensitiveValue secretKey, SensitiveValue dataKey)
        : accessKeyId_(std::move(accessKeyId)), secretKey_(std::move(secretKey)), dataKey_(std::move(dataKey)) {}

    const std::string &GetAccessKeyId() const
    {
        return accessKeyId_;
    }

    const SensitiveValue &GetSecretKey() const
    {
        return secretKey_;
    }

    const SensitiveValue &GetDataKey() const
    {
        return dataKey_;
    }
private:
    std::string accessKeyId_;
    SensitiveValue secretKey_;
    SensitiveValue dataKey_;
};

}
#endif // COMMON_KEY_FOR_AKSK_H
