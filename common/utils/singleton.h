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

#ifndef COMMON_UTILS_SINGLETON_H
#define COMMON_UTILS_SINGLETON_H

namespace functionsystem {

// Singletons of static variables cannot be interdependent,
// because the order of destructors of singletons is unpredictable and is not safe in multithreading.
template <typename T>
class Singleton {
public:
    virtual ~Singleton() = default;

    static T &GetInstance()
    {
        static T instance = T();
        return instance;
    }

    Singleton(const Singleton &) = delete;

    Singleton &operator=(const Singleton &) = delete;

protected:
    Singleton() = default;
};
}  // namespace functionsystem
#endif
