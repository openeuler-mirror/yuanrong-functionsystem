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

#include "udp_adapter.hpp"

#include <arpa/inet.h>
#include <memory>

#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

namespace litebus {
#define UC_MSG_HEADER_OFFSET sizeof(struct UCHeader)

// size of hpuc_mbuf_s
constexpr size_t UC_MSG_MBUF_OFFSET = 48;

constexpr uint32_t PACKET_FLAG = 0x1213F4F5;

bool CheckUdpPacket(const void *ucMsg, const uint32_t count, struct UCHeader *header)
{
    // don't check if the paramters are nullptr, because the caller will guarantee
    bool fPacket = ((header->packetFlag) != PACKET_FLAG);
    if (fPacket) {
        return false;
    }
    size_t total = UC_MSG_HEADER_OFFSET + header->msgNameLen + header->srcPidLen + header->destPidLen;
    if (total > count) {
        return false;
    }

    void *udpData = static_cast<void *>(header);
    bool fName = (header->msgNameLen >= count) || ((static_cast<char *>(udpData) +
                  UC_MSG_HEADER_OFFSET + header->msgNameLen) <= static_cast<const char *>(ucMsg)) ||
                 ((static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET) >=
                  (static_cast<const char *>(ucMsg) + count));
    if (fName) {
        return false;
    }

    bool fFrom = (header->srcPidLen >= count) ||
                 ((static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET +
                  header->msgNameLen + header->srcPidLen) <= static_cast<const char *>(ucMsg)) ||
                 ((static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET + header->msgNameLen) >=
                  (static_cast<const char *>(ucMsg) + count));
    if (fFrom) {
        return false;
    }

    bool fTo = (header->destPidLen >= count) ||
               ((static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET + header->msgNameLen + header->srcPidLen +
                 header->destPidLen) <= static_cast<const char *>(ucMsg)) ||
               ((static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET + header->msgNameLen + header->srcPidLen) >=
                (static_cast<const char *>(ucMsg) + count));

    if (fTo) {
        return false;
    }

    return true;
}

std::unique_ptr<MessageBase> Parse3rdMsg(const char *buf, uint32_t bufLen)
{
    std::unique_ptr<MessageBase> msg(new (std::nothrow) MessageBase());
    BUS_OOM_EXIT(msg);
    BUSLOG_DEBUG("parse msg, bufLen:{},minLen:{}", bufLen, sizeof(struct UCHeader));

    if (bufLen < UC_MSG_MBUF_OFFSET + sizeof(struct UCHeader)) {
        BUSLOG_ERROR("parse msg, bufLen < bufLen:{},maxLen:{}", bufLen, UC_MSG_MBUF_OFFSET + sizeof(struct UCHeader));
        return nullptr;
    }
    // size of hpuc_mbuf_s
    void *udpData = const_cast<char *>(buf) + UC_MSG_MBUF_OFFSET;

    auto *header = (struct UCHeader *)udpData;

    // check the validity of the receiving packet, in case that core dump in the
    // string constructor if the second parameter >= 256*1024.
    if (!CheckUdpPacket(const_cast<char *>(buf), bufLen, header)) {
        BUSLOG_ERROR("recv invalid packet, will drop it,len={}", bufLen);
        return nullptr;
    }

    char *cur = static_cast<char *>(udpData) + UC_MSG_HEADER_OFFSET;
    if (header->msgNameLen > strlen(cur)) {
        return nullptr;
    }
    msg->name = std::string(cur, header->msgNameLen);

    cur = cur + header->msgNameLen;

    char srcIPdotdec[IP_STR_LENGTH];
    (void)inet_ntop(AF_INET, static_cast<void *>(&header->srcIP), srcIPdotdec, IP_SIZE);
    if (header->srcPidLen > strlen(cur)) {
        return nullptr;
    }
    msg->from = std::string(cur, header->srcPidLen) + "@udp://" + srcIPdotdec + ":" + std::to_string(header->srcPort);

    cur = cur + header->srcPidLen;
    char destIPdotdec[IP_STR_LENGTH];
    (void)inet_ntop(AF_INET, static_cast<void *>(&header->destIP), destIPdotdec, IP_SIZE);
    if (header->destPidLen > strlen(cur)) {
        return nullptr;
    }
    msg->to = std::string(cur, header->destPidLen) + "@udp://" + srcIPdotdec + ":" + std::to_string(header->destPort);

    cur = cur + header->destPidLen;
    if ((header->dataSize > 0) && (header->dataSize < MAX_UDP_LEN)) {
        msg->body = std::string(cur, header->dataSize);
    }

    return msg;
}

}    // namespace litebus
