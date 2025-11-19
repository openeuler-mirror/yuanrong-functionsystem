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


#ifndef COMMON_SIGN_REQUEST_H
#define COMMON_SIGN_REQUEST_H

#include <map>
#include <memory>

namespace functionsystem {

class SignRequest {
public:
    SignRequest();

    SignRequest(const std::string &method, const std::string &path,
                const std::shared_ptr<std::map<std::string, std::string>> &queries,
                const std::map<std::string, std::string> &headers, const std::string &body)
        : method_(method), path_(path), queries_(queries), header_(headers), body_(body)
    {
    }

    std::string method_;
    std::string path_;
    std::shared_ptr<std::map<std::string, std::string>> queries_;
    std::map<std::string, std::string> header_;
    std::string body_;
};
}  // namespace functionsystem

#endif // COMMON_SIGN_REQUEST_H
