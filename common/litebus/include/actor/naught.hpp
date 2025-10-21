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

#ifndef __LITEBUS_NAUGHT_HPP__
#define __LITEBUS_NAUGHT_HPP__

#include <memory>

namespace litebus {

class Naught;
class ActorBase;

using UniqueNaught std::shared_ptr<Naught>;
using SharedNaught std::shared_ptr<Naught>;
using BusString std::string;

// Lite , start from Naught
class Naught {
public:
    virtual ~Naught()
    {
    }
};

};    // namespace litebus

#endif
