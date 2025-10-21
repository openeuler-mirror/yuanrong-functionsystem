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

#ifndef LITEBUS_UDP_ADAPTER_H
#define LITEBUS_UDP_ADAPTER_H
#include "actor/buslog.hpp"
#include "tcp/tcpmgr.hpp"

#include "udpmgr.hpp"

namespace litebus {

constexpr uint8_t IP_STR_LENGTH = 20;

constexpr uint8_t IP_SIZE = 16;

struct UCHeader {
    uint16_t msgNameLen;
    uint16_t type;
    uint32_t srcIP;
    uint16_t srcPort;
    uint16_t srcPidLen;
    uint32_t destIP;
    uint16_t destPort;
    uint16_t destPidLen;
    uint32_t dataSize;        // pb data size
    uint32_t dataBodySize;    // data body, can add data without pb
    uint32_t packetFlag;      // packet flag for verifying validity
};

std::unique_ptr<MessageBase> Parse3rdMsg(const char *buf, uint32_t bufLen);
bool CheckUdpPacket(const void *ucMsg, const int count, struct UCHeader *header);

}    // namespace litebus

#endif
