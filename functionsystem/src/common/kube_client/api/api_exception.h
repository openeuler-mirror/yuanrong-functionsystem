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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_API_EXCEPTION_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_API_EXCEPTION_H

#include <sstream>
#include <string>

namespace functionsystem::kube_client {
namespace utility {

class Datetime {
public:
    enum class DateFormat { ISO_8601 };
    Datetime()
    {
    }
    std::string ToString() const
    {
        return m_timeString;
    }
    static Datetime FromString(const std::string &timeString)
    {
        return Datetime(timeString);
    }
    bool IsInitialized() const
    {
        return m_timeString.empty();
    }

private:
    explicit Datetime(const std::string &timeString) : m_timeString(timeString)
    {
    }
    std::string m_timeString;
};
}  // namespace utility
}  // namespace functionsystem::kube_client

namespace functionsystem::kube_client {
namespace api {

class ApiException {
    ApiException();
    virtual ~ApiException();
};

}  // namespace api
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_API_EXCEPTION_H */
