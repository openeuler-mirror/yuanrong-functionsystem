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

#ifndef __LITEBUS_UDPMGR_H__
#define __LITEBUS_UDPMGR_H__

#include <map>
#include <set>
#include <string>

#include "actor/iomgr.hpp"
#include "circlebuf.hpp"
#include "evloop/evloop.hpp"
#include "iomgr/evbufmgr.hpp"
#include "iomgr/linkmgr.hpp"

namespace litebus {

// (80 * 1024)
const int MAX_UDP_LEN = 81920;

enum UDPErrorCode {
    SUCCESS_RTN = 0,
    FAIL_RTN = -1,
    FAIL_RULE_CONFLICT = -2,
    FAIL_OOM = -3, /* Out Of Memory */
};

/* One udp packet will generate one udp record */
struct UdpRecord {
    size_t pktLength;                           /* The length of send/recv packet */
    int ret;                                    /* The return code of API send/recvfrom */
    std::chrono::steady_clock::time_point when; /* When we send/recv this packet */
};

class UdpUtil {
public:
    static int SetSocket(int fd);
    static int CreateSocket(sa_family_t family);
    static void RecordSendUdpPkg(const std::map<std::string, CircleArray<UdpRecord> *> &sendRecord, int sendRules,
                                 const std::string &peer, size_t size, int ret);

    static void RecordRecvUdpPkg(const std::map<std::string, CircleArray<UdpRecord> *> &recvRecord, int recvRules,
                                 const std::string &peer, size_t size, int ret);
    static void RecordUdpPkg(std::map<std::string, CircleArray<UdpRecord> *> recordMap, const std::string &peer,
                             size_t size, int ret);

    static void LogRecordUdp(const UdpRecord *record);
    static bool WriteMsgMagicID(MsgHeader *header, const unsigned int headerLen);
    static bool WriteMsgHeader(char *cur, const unsigned int curLen, MsgHeader *header,
                               const std::unique_ptr<MessageBase> &msg, const std::string &to,
                               const std::string &from);
    static bool WriteMsgName(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg);
    static bool WriteMsgTo(char *cur, const unsigned int curLen, const std::string &to);
    static bool WriteMsgFrom(char *cur, const unsigned int curLen, const std::string &from);
    static bool WriteMsgSignature(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg);
    static bool WriteMsgBody(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg);
    static bool WriteMsgToBuf(char *buf, const unsigned int bufLen, const std::unique_ptr<MessageBase> &msg,
                              const std::string &to, const std::string &from);
    static std::unique_ptr<MessageBase> ParseMsg(char *buf, uint32_t bufLen);
    static bool ParseMsgHeader(const uint32_t &remainingLen, char *cur, MsgHeader **headerPtr);
};

class UDPMgr : public IOMgr {
public:
    UDPMgr() = default;
    ~UDPMgr() override;
    UDPMgr(const UDPMgr &) = delete;
    UDPMgr &operator=(const UDPMgr &) = delete;
    int Send(std::unique_ptr<MessageBase> &&msg, bool remoteLink = false, bool isExactNotRemote = false);
    void Link(const AID &sAid, const AID &dAid) override;
    void Reconnect(const AID &sAid, const AID &dAid) override;
    void UnLink(const AID &dAid) override;
    void RegisterMsgHandle(MsgHandler handler) override;
    bool Init() override;
    void Finish() override;
    void FinishDestruct();
    bool StartIOServer(const std::string &url, const std::string &advertiseUrl) override;
    uint64_t GetOutBufSize() override;
    uint64_t GetInBufSize() override;

    void CollectMetrics() override
    {
        return;
    }

    static void RecvMsg(int server, uint32_t events, void *arg);

    int AddRuleUdp(std::string peer, int recordNum) override;

    void DelRuleUdp(std::string peer, bool outputLog) override;

    void LinkRecycleCheck(int) override {};

private:
    // url=tcp@ip:port
    std::string url_;
    std::mutex sendMutex;
    int serverFd = -1;
    EvLoop *evloop = nullptr;
    char *recvBuf = nullptr;
    char *sendBuf = nullptr;

    static std::map<std::string, CircleArray<UdpRecord> *> sendRecord;
    static int sendRules;

    static std::map<std::string, CircleArray<UdpRecord> *> recvRecord;
    static int recvRules;

    static std::recursive_mutex sendRecordMutex; /* Protect _sendRecord */
    static std::recursive_mutex recvRecordMutex; /* Protect _recvRecord */
    friend class UdpUtil;
};

};    // namespace litebus

#endif
