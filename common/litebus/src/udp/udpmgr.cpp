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

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <securec.h>
#include <csignal>
#include <unistd.h>

#include <arpa/inet.h>

#include <sys/eventfd.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "tcp/tcpmgr.hpp"

#include "udp_adapter.hpp"

#include "udpmgr.hpp"

namespace litebus {

IOMgr::MsgHandler g_udpMsgHandler;

std::string g_udpAdvertiseUrl = "";

std::recursive_mutex UDPMgr::sendRecordMutex; /* Protect _sendRecord */
std::map<std::string, CircleArray<UdpRecord> *> UDPMgr::sendRecord;
int UDPMgr::sendRules;

std::recursive_mutex UDPMgr::recvRecordMutex; /* Protect _recvRecord */
std::map<std::string, CircleArray<UdpRecord> *> UDPMgr::recvRecord;
int UDPMgr::recvRules;

int UdpUtil::SetSocket(int fd)
{
    int optionVal = 1;
    int ret;

    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<char *>(&optionVal), sizeof(optionVal));
    if (ret) {
        BUSLOG_ERROR("setsockopt SO_REUSEPORT fail, fd:{},err:{}", fd, errno);
        return -1;
    }

    return 0;
}

int UdpUtil::CreateSocket(sa_family_t family)
{
    int ret;
    int fd;

    // create server socket
    fd = ::socket(family,
                  static_cast<int>(SOCK_DGRAM) | static_cast<int>(SOCK_NONBLOCK) | static_cast<int>(SOCK_CLOEXEC), 0);
    if (fd < 0) {
        BUSLOG_ERROR("create socket fail, err:{}", errno);
        return -1;
    }

    ret = SetSocket(fd);
    if (ret < 0) {
        (void)close(fd);
        return -1;
    }

    return fd;
}

void UdpUtil::RecordSendUdpPkg(const std::map<std::string, CircleArray<UdpRecord> *> &sendRecord, int sendRules,
                               const std::string &peer, size_t size, int ret)
{
    if (sendRules <= 0) {
        return;
    }
    RecordUdpPkg(sendRecord, peer, size, ret);
}

void UdpUtil::RecordRecvUdpPkg(const std::map<std::string, CircleArray<UdpRecord> *> &recvRecord, int recvRules,
                               const std::string &peer, size_t size, int ret)
{
    if (recvRules <= 0) {
        return;
    }
    RecordUdpPkg(recvRecord, peer, size, ret);
}

void UdpUtil::RecordUdpPkg(std::map<std::string, CircleArray<UdpRecord> *> recordMap, const std::string &peer,
                           size_t size, int ret)
{
    if (recordMap.find(peer) != recordMap.end()) {
        CircleArray<UdpRecord> *record = recordMap[peer];
        if (record == nullptr) {
            return;
        }
        UdpRecord *element = record->NextElement();
        if (element == nullptr) {
            return;
        }

        element->pktLength = size;
        element->ret = ret;
        element->when = std::chrono::steady_clock::now();
    }
}

void UdpUtil::LogRecordUdp(const UdpRecord *record)
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    BUSLOG_ERROR("UDP pkg:pktlen,ret,b,p={},r={},b={} ms", record->pktLength, record->ret,
                 std::chrono::duration_cast<std::chrono::milliseconds>(now - record->when).count());
}

bool UdpUtil::WriteMsgMagicID(MsgHeader *header, const unsigned int headerLen)
{
    errno_t ret = memcpy_s(header, headerLen, BUS_MAGICID.data(), BUS_MAGICID.size());
    if (ret != EOK) {
        BUSLOG_ERROR("copy magic to buf failed,errno:{},magic:{}", ret, BUS_MAGICID);
        return false;
    }
    return true;
}

bool UdpUtil::WriteMsgHeader(char *cur, const unsigned int, MsgHeader *header,
                             const std::unique_ptr<MessageBase> &msg, const std::string &to, const std::string &from)
{
    errno_t ret;
    BUSLOG_DEBUG("nameLen:,toLen:,fromLen:,bodyLen:{},{},{},{}", msg->name.size(), to.size(), from.size(),
                 msg->body.size());

    header->nameLen = htonl(static_cast<uint32_t>(msg->name.size()));
    header->toLen = htonl(static_cast<uint32_t>(to.size()));
    header->fromLen = htonl(static_cast<uint32_t>(from.size()));
    header->signatureLen = htonl(static_cast<uint32_t>(msg->signature.size()));
    header->bodyLen = htonl(static_cast<uint32_t>(msg->body.size()));

    ret = memcpy_s(cur, MAX_UDP_LEN, header, sizeof(MsgHeader));
    if (ret != EOK) {
        BUSLOG_ERROR("copy MsgHeader to buf failed,errno:{},size(msgheader):{}", ret, sizeof(MsgHeader));
        return false;
    }
    return true;
}

bool UdpUtil::WriteMsgName(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg)
{
    errno_t ret = memcpy_s(cur, curLen, msg->name.data(), msg->name.size());
    if (ret != EOK) {
        BUSLOG_ERROR("copy name to buf failed,errno:{},name:{}", ret, msg->name);
        return false;
    }
    return true;
}

bool UdpUtil::WriteMsgTo(char *cur, const unsigned int curLen, const std::string &to)
{
    errno_t ret = memcpy_s(cur, curLen, to.data(), to.size());
    if (ret != EOK) {
        BUSLOG_ERROR("copy TO to buf failed,errno:{},to:{}", ret, to);
        return false;
    }
    return true;
}

bool UdpUtil::WriteMsgFrom(char *cur, const unsigned int curLen, const std::string &from)
{
    errno_t ret = memcpy_s(cur, curLen, from.data(), from.size());
    if (ret != EOK) {
        BUSLOG_ERROR("copy from to buf failed,errno:{},from:{}", ret, from);
        return false;
    }
    return true;
}

bool UdpUtil::WriteMsgSignature(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg)
{
    errno_t ret;
    if (msg->signature.size() > 0) {
        ret = memcpy_s(cur, curLen, msg->signature.data(), msg->signature.size());
        if (ret != EOK) {
            BUSLOG_ERROR("copy signature to buf failed,errno:{},signature size:{}", ret, msg->signature.size());
            return false;
        }
    }
    return true;
}

bool UdpUtil::WriteMsgBody(char *cur, const unsigned int curLen, const std::unique_ptr<MessageBase> &msg)
{
    errno_t ret;
    if (msg->body.size() > 0) {
        ret = memcpy_s(cur, curLen, msg->body.data(), msg->body.size());
        if (ret != EOK) {
            BUSLOG_ERROR("copy body to buf failed,errno:{},body size:{}", ret, msg->body.size());
            return false;
        }
    }
    return true;
}

// write message to the buffer
bool UdpUtil::WriteMsgToBuf(char *buf, const unsigned int bufLen, const std::unique_ptr<MessageBase> &msg,
                            const std::string &to, const std::string &from)
{
    if (buf == nullptr) {
        return false;
    }
    char *cur = buf;
    unsigned int curLen = bufLen;
    MsgHeader header;
    litebus::InitMsgHeader(header);
    State state = State::MAGICID;

    // run state machine
    switch (state) {
        case State::MAGICID:
            if (!WriteMsgMagicID(&header, sizeof(header))) {
                return false;
            }
            state = State::MSG_HEADER;
        /* fall through */
        case State::MSG_HEADER:
            if (!WriteMsgHeader(cur, bufLen, &header, msg, to, from)) {
                return false;
            }
            cur = cur + sizeof(MsgHeader);
            curLen = bufLen - sizeof(MsgHeader);
            state = State::NAME;
        /* fall through */
        case State::NAME:
            if (!WriteMsgName(cur, curLen, msg)) {
                return false;
            }
            cur = cur + msg->name.size();
            curLen = curLen - msg->name.size();
            state = State::TO;
        /* fall through */
        case State::TO:
            if (!WriteMsgTo(cur, curLen, to)) {
                return false;
            }
            cur = cur + to.size();
            curLen = curLen - to.size();
            state = State::FROM;
        /* fall through */
        case State::FROM:
            if (!WriteMsgFrom(cur, curLen, from)) {
                return false;
            }
            cur = cur + from.size();
            curLen = curLen - from.size();
            state = State::SIGNATURE;
            /* fall through */
        case State::SIGNATURE:
            if (!WriteMsgSignature(cur, curLen, msg)) {
                return false;
            }
            cur = cur + msg->signature.size();
            curLen = curLen - msg->signature.size();
            state = State::BODY;
        /* fall through */
        default:
            if (!WriteMsgBody(cur, curLen, msg)) {
                return false;
            }
    }    // end switch

    return true;
}

bool UdpUtil::ParseMsgHeader(const uint32_t &remainingLen, char *cur, MsgHeader **headerPtr)
{
    if (remainingLen < sizeof(MsgHeader)) {
        BUSLOG_ERROR("remainingLen(r),sizeof MsgHeader(s),state(t):r:{},s:{},t:{}", remainingLen, sizeof(MsgHeader),
                     static_cast<std::underlying_type_t<State>>(MSG_HEADER));
        return false;
    }
    *headerPtr = reinterpret_cast<MsgHeader *>(cur);
    if (*headerPtr == nullptr) {
        return false;
    }
    MsgHeader *header = *headerPtr;
    EvbufMgr::HeaderNtoH(header);
    if (header->nameLen > MAX_KMSG_NAME_LEN || header->toLen > MAX_KMSG_TO_LEN || header->fromLen > MAX_KMSG_FROM_LEN ||
        header->bodyLen > MAX_KMSG_BODY_LEN || header->signatureLen > MAX_KMSG_SIGNATURE_LEN) {
        BUSLOG_ERROR("Drop invalid udp data. length out of range");
        return false;
    }
    BUSLOG_DEBUG("recv nameLen(nl),toLen(tl),fromLen(fl),bodyLen(bl):nl:{},tl:{},fl:{},bl:{}", header->nameLen,
                 header->toLen, header->fromLen, header->bodyLen);
    uint32_t payloadLen = remainingLen - sizeof(MsgHeader);
    if (payloadLen != (header->nameLen + header->toLen + header->fromLen + header->signatureLen + header->bodyLen)) {
        BUSLOG_DEBUG(
            "check msg len "
            "fail,remainingLen:(r),nameLen(nl),toLen(tl),fromLen(fl),bodyLen(bl):r:{},nl:{},tl:{},fl:{},bl:{}",
            payloadLen, header->nameLen, header->toLen, header->fromLen, header->bodyLen);
        return false;
    }
    return true;
}

std::unique_ptr<MessageBase> UdpUtil::ParseMsg(char *buf, uint32_t bufLen)
{
    char *cur = buf;
    State state;
    uint32_t remainingLen;
    std::unique_ptr<MessageBase> msg(new (std::nothrow) MessageBase());
    BUS_OOM_EXIT(msg);

    remainingLen = bufLen;
    state = MSG_HEADER;
    MsgHeader *header = nullptr;

    switch (state) {
        case MSG_HEADER:
            if (!ParseMsgHeader(remainingLen, cur, &header)) {
                return nullptr;
            }
            cur = cur + sizeof(MsgHeader);
            remainingLen = remainingLen - sizeof(MsgHeader);
            state = NAME;
        /* fall through */
        case NAME:
            msg->name = std::string(cur, header->nameLen);
            cur = cur + header->nameLen;
            remainingLen = remainingLen - header->nameLen;
            state = TO;
        /* fall through */
        case TO:
            msg->to = std::string(cur, header->toLen);
            cur = cur + header->toLen;
            remainingLen = remainingLen - header->toLen;
            state = FROM;
        /* fall through */
        case FROM:
            msg->from = std::string(cur, header->fromLen);
            cur = cur + header->fromLen;
            remainingLen = remainingLen - header->fromLen;
            state = SIGNATURE;
        /* fall through */
        case SIGNATURE:
            msg->signature = std::string(cur, header->signatureLen);
            cur = cur + header->signatureLen;
            remainingLen = remainingLen - header->signatureLen;
            state = BODY;
        /* fall through */
        default:
            msg->body = std::string(cur, header->bodyLen);
    }

    return msg;
}

UDPMgr::~UDPMgr()
{
    try {
        FinishDestruct();
    } catch (...) {
        // Ignore
    }
}

void UDPMgr::RegisterMsgHandle(MsgHandler handler)
{
    g_udpMsgHandler = handler;
}

bool UDPMgr::Init()
{
    do {
        sendBuf = new (std::nothrow) char[MAX_UDP_LEN];
        if (sendBuf == nullptr) {
            BUSLOG_ERROR("new sendBuf failed");
            break;
        }
        // 1. dest is valid 2. destsz equals to count and both are valid.
        // memset_s will always execute successfully.
        (void)memset_s(sendBuf, MAX_UDP_LEN, 0, MAX_UDP_LEN);

        recvBuf = new (std::nothrow) char[MAX_UDP_LEN];
        if (recvBuf == nullptr) {
            BUSLOG_ERROR("new recvBuf failed");
            break;
        }
        // 1. dest is valid 2. destsz equals to count and both are valid.
        // memset_s will always executes successfully.
        (void)memset_s(recvBuf, MAX_UDP_LEN, 0, MAX_UDP_LEN);

        evloop = new (std::nothrow) EvLoop();
        if (evloop == nullptr) {
            BUSLOG_ERROR("new EvLoop failed");
            break;
        }

        bool ok = evloop->Init(UDP_EVLOOP_THREADNAME);
        if (!ok) {
            BUSLOG_ERROR("EvLoop init failed");
            break;
        }
        BUSLOG_INFO("init succ");
        return true;
    } while (false);

    if (recvBuf != nullptr) {
        delete[] recvBuf;
        recvBuf = nullptr;
    }
    if (sendBuf != nullptr) {
        delete[] sendBuf;
        sendBuf = nullptr;
    }
    if (evloop != nullptr) {
        delete evloop;
        evloop = nullptr;
    }
    serverFd = -1;
    return false;
}

void UDPMgr::RecvMsg(int server, uint32_t events, void *arg)
{
    char *buf = static_cast<char *>(arg);
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);

    BUSLOG_DEBUG("events:{},server:{}", events, server);
    // check magic
    if (buf == nullptr) {
        return;
    }

    ssize_t count = 0;
    count = recvfrom(server, buf, MAX_UDP_LEN, 0, (struct sockaddr *)&fromAddr, &fromLen);
    if (count == -1) {
        BUSLOG_WARN("recv failed,return:{},errno:{}", std::to_string(count), errno);
        return;
    }
    char iPdotdec[IP_STR_LENGTH];
    (void)inet_ntop(AF_INET, static_cast<void *>(&(fromAddr.sin_addr)), iPdotdec, IP_STR_LENGTH);

    BUSLOG_DEBUG("recv udp packet, count:{},ip:{},port:{}", std::to_string(count), iPdotdec,
                 std::to_string(ntohs(fromAddr.sin_port)));

    std::unique_ptr<MessageBase> msg;

    auto magicSize = static_cast<ssize_t>(BUS_MAGICID.size());
    if (count > magicSize && 0 == strncmp(buf, BUS_MAGICID.data(), BUS_MAGICID.size())) {
        msg = UdpUtil::ParseMsg(buf, static_cast<uint32_t>(count));
    } else {
        msg = Parse3rdMsg(buf, static_cast<uint32_t>(count));
    }

    if (msg == nullptr) {
        return;
    }

    bool validAid = msg->from.OK() && msg->to.OK();
    if (!validAid) {
        BUSLOG_ERROR("from/to is invalid,from:{},to:{}", std::string(msg->from), std::string(msg->to));
        return;
    }

    // change message type from KMSG to KUDP
    msg->type = MessageBase::Type::KUDP;

    recvRecordMutex.lock();
    UdpUtil::RecordRecvUdpPkg(recvRecord, recvRules, msg->from.UnfixUrl(), msg->body.size(), count);
    recvRecordMutex.unlock();

    BUSLOG_DEBUG("message,name:{},from:{},to:{}", msg->name, std::string(msg->from), std::string(msg->to));

    if (g_udpMsgHandler != nullptr) {
        g_udpMsgHandler(std::move(msg));
    } else {
        BUSLOG_INFO("g_udpMsgHandler was not found");
    }
}

bool UDPMgr::StartIOServer(const std::string &url, const std::string &advertiseUrl)
{
    IOSockaddr addr;
    url_ = url;
    g_udpAdvertiseUrl = advertiseUrl;

    if (g_udpAdvertiseUrl.empty()) {
        g_udpAdvertiseUrl = url_;
    }
    if (!SocketOperate::GetSockAddr(url_, addr)) {
        return false;
    }

    // create server socket
    serverFd = UdpUtil::CreateSocket(addr.sa.sa_family);
    if (serverFd < 0) {
        BUSLOG_ERROR("create socket fail,errno,url,advertiseUrl:e:{},u:{},s:{}", errno, url_, g_udpAdvertiseUrl);
        return false;
    }

    // bind
    if (::bind(serverFd, (struct sockaddr *)&addr, sizeof(IOSockaddr))) {
        BUSLOG_ERROR("bind fail,errno,url,advertiseUrl:e:{},u:{},s:{}", errno, url_, g_udpAdvertiseUrl);
        return false;
    }

    // register read event callback for server socket
    int retval =
        evloop->AddFdEvent(serverFd, static_cast<unsigned int>(EPOLLIN), RecvMsg, static_cast<void *>(recvBuf));
    if (retval != BUS_OK) {
        BUSLOG_ERROR("add event failed,url,serverFd:u:{},s:{}", url_, serverFd);
        return false;
    }
    BUSLOG_INFO("start server succ,url:{},advertiseUrl:{}", url_, g_udpAdvertiseUrl);

    return true;
}

int UDPMgr::Send(std::unique_ptr<MessageBase> &&msg, bool, bool)
{
    IOSockaddr toAddr;
    std::string from = msg->from.Name() + "@" + g_udpAdvertiseUrl;
    std::string to = msg->to;
    if (msg->name.size() > MAX_KMSG_NAME_LEN || to.size() > MAX_KMSG_TO_LEN || from.size() > MAX_KMSG_FROM_LEN ||
        msg->body.size() > MAX_KMSG_BODY_LEN || msg->signature.size() > MAX_KMSG_SIGNATURE_LEN) {
        BUSLOG_ERROR("Drop invalid udp data. length out of range");
        return UDP_MSG_TOO_BIG;
    }
    size_t sendLen = sizeof(MsgHeader) + std::string(msg->name).size() + to.size() + from.size() +
                     msg->signature.size() + msg->body.size();
    if (sendLen > MAX_UDP_LEN) {
        BUSLOG_ERROR("sendlen,maxsendlen,name,from,to,bodysize:sl:{},maxsl:{},n:{},f:{},to:{},signature:{},sz:{}",
                     sendLen, MAX_UDP_LEN, msg->name, from, to, msg->signature.size(), msg->body.size());
        return UDP_MSG_TOO_BIG;
    }

    if (!SocketOperate::GetSockAddr(to, toAddr)) {
        return UDP_MSG_ADDR_ERR;
    }

    BUSLOG_DEBUG("send msg, name:{},f:{},to:{},sz:{}", msg->name, from, to, sendLen);
    std::lock_guard<std::mutex> lock(sendMutex);

    bool ok = UdpUtil::WriteMsgToBuf(static_cast<char *>(sendBuf), MAX_UDP_LEN, msg, to, from);
    if (!ok) {
        BUSLOG_ERROR("WriteMsgToBuf fail,from,to,len:f:{},t:{},l:{}", from, to, sendLen);
        return UDP_MSG_WRITE_ERR;
    }

    int sendRet = 0;
    int ret = sendto(serverFd, sendBuf, sendLen, 0, (struct sockaddr *)&toAddr, sizeof(toAddr));
    if (ret < 0) {
        BUSLOG_ERROR("sendto fail,errno:{},from:{},to:{},len:{}", errno, from, to, sendLen);
        sendRet = UDP_MSG_SEND_ERR;
    } else {
        BUSLOG_DEBUG("sendto succ,from:{},to:{},len:{}", from, to, sendLen);
        sendRet = UDP_MSG_SEND_SUCCESS;
    }
    UDPMgr::sendRecordMutex.lock();
    UdpUtil::RecordSendUdpPkg(sendRecord, sendRules, msg->to.UnfixUrl(), msg->body.size(), ret);
    UDPMgr::sendRecordMutex.unlock();
    return sendRet;
}

void UDPMgr::Link(const AID &, const AID &)
{
    // do nothing
}

void UDPMgr::UnLink(const AID &)
{
    // do nothing
}

void UDPMgr::Reconnect(const AID &, const AID &)
{
    // do nothing
}

void UDPMgr::FinishDestruct()
{
    if (evloop != nullptr) {
        evloop->Finish();
        if (evloop->DelFdEvent(serverFd) != BUS_OK) {
            BUSLOG_ERROR("failed to delete server fd event");
        }
        delete evloop;
        evloop = nullptr;
    }

    if (recvBuf != nullptr) {
        delete[] recvBuf;
        recvBuf = nullptr;
    }
    if (sendBuf != nullptr) {
        delete[] sendBuf;
        sendBuf = nullptr;
    }
    if (serverFd > 0) {
        (void)close(serverFd);
        serverFd = -1;
    }
}

void UDPMgr::Finish()
{
    FinishDestruct();
}

uint64_t UDPMgr::GetOutBufSize()
{
    return 1;
}
uint64_t UDPMgr::GetInBufSize()
{
    return 1;
}

int UDPMgr::AddRuleUdp(std::string peer, int recordNum)
{
    CircleArray<UdpRecord> *record = nullptr;

    if (recordNum > 0) {
        sendRecordMutex.lock();
        if (sendRecord.find(peer) != sendRecord.end()) {
            sendRecordMutex.unlock();
            BUSLOG_ERROR("peer already exist, p:{}", peer);
            return static_cast<int>(UDPErrorCode::FAIL_RULE_CONFLICT);
        }
        record = new (std::nothrow) CircleArray<UdpRecord>(recordNum);
        if (record == nullptr) {
            sendRecordMutex.unlock();
            BUSLOG_ERROR("record is null!");
            return static_cast<int>(UDPErrorCode::FAIL_OOM);
        }
        sendRecord[peer] = record;

        ++sendRules;
        sendRecordMutex.unlock();

        recvRecordMutex.lock();
        record = new (std::nothrow) CircleArray<UdpRecord>(recordNum);
        if (record == nullptr) {
            /* It is necessary to record sending and receiving information at the
               same time otherwise it does not make sense */
            sendRecordMutex.lock();
            delete sendRecord[peer];
            (void)sendRecord.erase(peer);
            --sendRules;
            sendRecordMutex.unlock();
            recvRecordMutex.unlock();
            BUSLOG_ERROR("record is null!");
            return static_cast<int>(UDPErrorCode::FAIL_OOM);
        }
        recvRecord[peer] = record;
        ++recvRules;
        recvRecordMutex.unlock();
    }

    return static_cast<int>(UDPErrorCode::SUCCESS_RTN);
}

void UDPMgr::DelRuleUdp(std::string peer, bool outputLog)
{
    CircleArray<UdpRecord> *record = nullptr;

    sendRecordMutex.lock();
    if (sendRecord.find(peer) != sendRecord.end()) {
        record = sendRecord[peer];
        (void)sendRecord.erase(peer);
        --sendRules;
        if (record != nullptr) {
            if (outputLog) {
                BUSLOG_ERROR("sent to udp peer ip:v:{}", peer);
                record->TraverseElements(UdpUtil::LogRecordUdp);
            }
            delete record;
        }
    }
    sendRecordMutex.unlock();

    recvRecordMutex.lock();
    if (recvRecord.find(peer) != recvRecord.end()) {
        record = recvRecord[peer];
        (void)recvRecord.erase(peer);
        --recvRules;
        if (record != nullptr) {
            if (outputLog) {
                BUSLOG_ERROR("Recv from udp peer ip:v:{}", peer);
                record->TraverseElements(UdpUtil::LogRecordUdp);
            }
            delete record;
        }
    }
    recvRecordMutex.unlock();
}

}    // namespace litebus
