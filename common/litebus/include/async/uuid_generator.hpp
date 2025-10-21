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

#ifndef UUID_GENERATOR_HPP
#define UUID_GENERATOR_HPP

#include "uuid_base.hpp"

namespace litebus {

namespace uuid_generator {
struct UUID : public litebus::uuids::uuid {
public:
    explicit UUID(const litebus::uuids::uuid &inputUUID) : litebus::uuids::uuid(inputUUID)
    {
    }
    static UUID GetRandomUUID();
    std::string ToString() const;
};
}    // namespace uuid_generator

namespace localid_generator {
int GenLocalActorId();

#ifdef HTTP_ENABLED
int GenHttpClientConnId();
int GenHttpServerConnId();
#endif

}    // namespace localid_generator

}    // namespace litebus
#endif
