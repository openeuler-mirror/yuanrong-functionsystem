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

#ifndef FUNCTION_MASTER_META_STORE_META_STORE_COMMON_H
#define FUNCTION_MASTER_META_STORE_META_STORE_COMMON_H

#include <cstdint>

namespace functionsystem::meta_store {
constexpr uint64_t META_STORE_CLUSTER_ID = 123456;
constexpr uint64_t META_STORE_MEMBER_ID = 456789;

constexpr int64_t META_STORE_REVISION = 32;
constexpr uint64_t META_STORE_RAFT_TERM = 2;
}

#endif // FUNCTION_MASTER_META_STORE_META_STORE_COMMON_H
