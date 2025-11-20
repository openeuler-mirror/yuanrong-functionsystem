# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.16.1)
project(kernel LANGUAGES CXX C)

include_directories(common/litebus/include)
include_directories(common/logs/include)
include_directories(common/meta_store/client/cpp/include)
include_directories(common/meta_store/server/cpp/include)
include_directories(common/utils/)
include_directories(common/utils/proto/pb/)
include_directories(common/utils/metadata)
include_directories(posix/)
include_directories(posix/proto/pb)
include_directories(functionsystem/src/)
include_directories(functionsystem/src/common)
include_directories(functionsystem/src/domain_scheduler)
include_directories(functionsystem/src/function_agent)
include_directories(functionsystem/src/function_proxy)
include_directories(functionsystem/src/function_proxy/local_scheduler/)
include_directories(functionsystem/src/function_master)


include_directories(vendor/out/openEuler/Install/cjson/include)
include_directories(vendor/out/openEuler/Install/grpc/include)
include_directories(vendor/out/openEuler/Install/gtest_1_10_0/include)
include_directories(vendor/out/openEuler/Install/gtest_1_12_1/include)
include_directories(vendor/out/openEuler/Install/protobuf/include)
include_directories(vendor/out/openEuler/Install/spdlog/include)

file(GLOB_RECURSE SOURCES1 functionsystem/src/*.cpp)
file(GLOB_RECURSE SOURCES2 functionsystem/src/*.cc)
file(GLOB_RECURSE SOURCES3 functionsystem/src/*.h)
file(GLOB_RECURSE SOURCES4 common/litebus/src/*.cpp)
file(GLOB_RECURSE SOURCES5 common/litebus/src/*.hpp)
file(GLOB_RECURSE SOURCES6 common/metrics/src/*.h)
file(GLOB_RECURSE SOURCES7 common/metrics/src/*.cpp)
file(GLOB_RECURSE SOURCES8 common/meta_store/client/src/*.cpp)
file(GLOB_RECURSE SOURCES9 common/meta_store/client/src/*.h)
file(GLOB_RECURSE SOURCES10 common/meta_store/server/src/*.cpp)
file(GLOB_RECURSE SOURCES11 common/meta_store/server/src/*.h)

add_executable(main ${SOURCES1} ${SOURCES2} ${SOURCES3} ${SOURCES4} ${SOURCES5} ${SOURCES6} ${SOURCES7} ${SOURCES8} ${SOURCES9} ${SOURCES10} ${SOURCES11})