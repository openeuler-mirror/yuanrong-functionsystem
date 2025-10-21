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

#ifndef __LITEBUS_LINKMGR_H__
#define __LITEBUS_LINKMGR_H__
#ifdef SSL_ENABLED

#include "openssl/ssl.h"
#endif

#include <map>
#include <queue>
#include <set>
#include <string>

#include <sys/socket.h>

#include "securec.h"

#include "actor/iomgr.hpp"

#include "evloop/evloop.hpp"
#include "iomgr/socket_operate.hpp"

namespace litebus {
namespace http {
class HttpParser;
}

using LinkerCallBack = void (*)(const std::string &from, const std::string &to);

using ConnectionCallBack = void (*)(void *conn);

constexpr int SENDMSG_IOVLEN = 6;
constexpr int RECVMSG_IOVLEN = 5;

constexpr unsigned int BUSMAGIC_LEN = 4;
constexpr int SENDMSG_QUEUELEN = 1024;
constexpr int SENDMSG_DROPED = -1;

constexpr size_t MAX_KMSG_FROM_LEN = 1024;
constexpr size_t MAX_KMSG_TO_LEN = 1024;
constexpr size_t MAX_KMSG_NAME_LEN = 1024;
constexpr size_t MAX_KMSG_BODY_LEN = 104857600;
constexpr size_t MAX_KMSG_SIGNATURE_LEN = 2 * 1024;

enum ParseType { KMSG = 1, KHTTP_REQ, KHTTP_RSP, UNKNOWN };

enum State { MAGICID = 1, MSG_HEADER, NAME, TO, FROM, SIGNATURE, BODY };

enum ConnectionState { INIT = 1, CONNECTING, CONNECTED, DISCONNECTING, CLOSE };

enum ConnectionType { TYPE_TCP = 1, TYPE_SSL };

enum ConnectionPriority { PRI_LOW = 1, PRI_HIGH };

struct MsgHeader {
    char magic[BUSMAGIC_LEN];
    uint32_t nameLen = 0;
    uint32_t toLen = 0;
    uint32_t fromLen = 0;
    uint32_t signatureLen = 0;
    uint32_t bodyLen = 0;
};

inline void InitMsgHeader(MsgHeader &msghdr)
{
    for (unsigned int i = 0; i < BUSMAGIC_LEN; i++) {
        if (i < BUS_MAGICID.size()) {
            msghdr.magic[i] = BUS_MAGICID[i];
        } else {
            msghdr.magic[i] = '\0';
        }
    }
}

struct SendMetrics {
    SendMetrics()
        : sendSum(0), sendMaxSize(0), errCode(0), lastSucMsgName(""), lastFailMsgName(""), lastSendMsgName("")
    {
    }

    void UpdateMax(int size)
    {
        sendSum++;
        if (size > sendMaxSize) {
            sendMaxSize = size;
        }
    }

    void UpdateName(const std::string &name)
    {
        lastSendMsgName = name;
    }

    void UpdateError(bool fail, int err = 0)
    {
        if (fail) {
            lastFailMsgName = lastSendMsgName;
            errCode = err;
        } else {
            lastSucMsgName = lastSendMsgName;
        }
    }

    void Refresh()
    {
        sendSum = 0;
        sendMaxSize = 0;
        errCode = 0;
        lastSucMsgName = "";
        lastFailMsgName = "";
        lastSendMsgName = "";
    }

    int sendSum;
    int sendMaxSize;
    int errCode;
    std::string lastSucMsgName;
    std::string lastFailMsgName;
    std::string lastSendMsgName;
};

class Connection {
public:
    Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    ~Connection();
    bool Different(const Connection *that)
    {
        return !(that != nullptr && that->to == to && that->isRemote == isRemote);
    }

    int fd;
    bool isRemote;
    bool isExited;
    ConnectionType type;
    // "tcp://ip:port"
    std::string from;
    std::string to;
    std::string peer;

    SocketOperate *socketOperate = nullptr;

    State recvState;

    std::string recvTo;
    std::string recvFrom;
    uint32_t recvLen;

    MsgHeader recvHeader;
    struct msghdr recvMsg;
    struct iovec recvIov[RECVMSG_IOVLEN];
    uint32_t recvTotalLen = 0;
    MessageBase *recvMsgBase;

    std::string sendTo;
    std::string sendFrom;

    MsgHeader sendHeader;
    struct msghdr sendMsg;
    struct iovec sendIov[SENDMSG_IOVLEN];
    uint32_t sendTotalLen = 0;

    SendMetrics *sendMetrics = nullptr;

    MessageBase *sendMsgBase = nullptr;
    ParseType recvMsgType;

    EvLoop *recvEvloop = nullptr;
    EvLoop *sendEvloop = nullptr;

    ConnectionCallBack eventCallBack = nullptr;
    ConnectionCallBack succCallBack = nullptr;
    ConnectionCallBack writeCallBack = nullptr;
    ConnectionCallBack readCallBack = nullptr;

    ConnectionState connState;

    std::queue<MessageBase *> sendQueue;
#ifdef HTTP_ENABLED
    http::HttpParser *decoder = nullptr;
    int sequence = 0;
    bool meetMaxClients = false;
    bool parseFailed = false;
#endif

    uint64_t outBufferSize = 0;
#ifdef SSL_ENABLED
    std::string credencial;
    SSL *ssl = nullptr;
#endif

    int errCode;
    ConnectionPriority priority;
    int noCommTime = 0;
    bool timeoutRemoved = false;
};

struct LinkerInfo {
    int fd;
    // actorname@tcp://ip:port
    std::string from;
    std::string to;
    LinkerCallBack delcb;    // del call back
};

class LinkMgr {
public:
    LinkMgr() : doubleLink(false)
    {
    }
    ~LinkMgr();

    void CloseConnection(Connection *conn);
    void SetLinkPattern(bool linkPattern);

    // link mgr
    bool SwapLinkerSocket(int fromFd, int toFd);
    void AddLinker(int fd, const AID &sAid, const AID &dAid, LinkerCallBack delcb);
    void DeleteLinker(int fd);
    void DeleteLinker(const std::string &to, int fd);
    void DeleteAllLinker();
    LinkerInfo *FindLinker(int fd, const AID &sAid, const AID &dAid);

    Connection *ExactFindLink(const std::string &to, bool remoteLink);
    Connection *FindLink(const std::string &to, bool remoteLink);
    Connection *FindLink(const std::string &to, bool remoteLink, bool exactNotRemote);

    // metrics analyze
    Connection *FindMaxLink();
    Connection *FindFastLink();
    void RefreshMetrics();

    void ExactDeleteLink(const std::string &to, bool remoteLink);
    void DeleteAllLink(std::map<std::string, Connection *> &alllinks);
    void AddLink(Connection *conn);
    void AddRemoteLink(Connection *conn);
    void DelRemoteLink(const Connection *conn);
    int GetRemoteLinkCount() const;
    void AddHttpRemoteLink(Connection *conn);
    void SetLinkPriority(const std::string &to, bool remoteLink, ConnectionPriority pri);

    // to_url=tcp@ip:port, event struct
    std::map<std::string, Connection *> links;

    // remote_url,connection
    std::map<std::string, Connection *> remoteLinks;

    // all remote connection for link recycle check
    std::map<int, Connection *> allRemoteLinks;

    // http remote connection for link recycle check
    std::map<int, Connection *> httpRemoteLinks;

    // each to_url has two fds at most, and each fd has multiple linkinfos
    std::map<int, std::set<LinkerInfo *>> linkers;

    static LinkMgr *GetLinkMgr();
    static void SetLinkMgr(LinkMgr *linkmgr);

private:
    bool doubleLink;
    static LinkMgr *linkMgr;
    static std::mutex linkMutex;
    friend class ConnectionUtil;
    friend class IOMgr;
    friend class TCPMgr;
};

class ConnectionUtil {    // recv msg
public:
    static void CloseConnection(Connection *conn);

    static void CheckRecvMsgType(Connection *conn);
    static int RecvKMsg(Connection *conn, IOMgr::MsgHandler msgHandler);
    static bool Parse(int, Connection *conn);
    static bool ParseHeader(Connection *conn, uint32_t &recvLen, int &retval);
    static void SetSocketOperate(Connection *conn);

    static int RecvMsg(Connection *conn);
    static void SocketEventHandler(int fd, uint32_t events, void *context);
    static int AddSockEventHandler(Connection *conn);
    static void NewConnEventHandler(int fd, uint32_t events, void *context);
    static int AddNewConnEventHandler(Connection *conn);
    static bool ConnEstablishedDelAdd(Connection *conn, int fd, uint32_t events, int *soError, uint32_t error);

private:
    static void CleanUp(int fd, Connection *conn);
};    // namespace connectionUtil

};    // namespace litebus

#endif
