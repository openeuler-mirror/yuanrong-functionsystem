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

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "actor/buslog.hpp"
#include "evbufmgr.hpp"

#include "actor/aid.hpp"
#include "actor/msg.hpp"

#include "tcp/tcp_socket.hpp"
#include "linkmgr.hpp"
#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"
#include "ssl/ssl_socket.hpp"
#endif
#ifdef HTTP_ENABLED
#include "httpd/http_parser.hpp"
#endif

namespace litebus {
LinkMgr *LinkMgr::linkMgr = nullptr;
std::mutex LinkMgr::linkMutex;

void LinkMgr::SetLinkPattern(bool linkPattern)
{
    doubleLink = linkPattern;
}

void LinkMgr::CloseConnection(Connection *conn)
{
    uint32_t freeMsgNum = 0;
    if (conn == nullptr) {
        return;
    }

    if (conn->recvEvloop != nullptr) {
        (void)conn->recvEvloop->DelFdEvent(conn->fd);
    }

    // trigger Exit message note that this should be called before erasing link. Because we may chang isExited flag
    // by to in this fun. And if isExited has been set to true, it means Exit message has been send before, do nothing.
    if (!conn->isExited) {
        DeleteLinker(conn->to, conn->fd);
    }

    if (conn->recvMsgBase != nullptr) {
        delete conn->recvMsgBase;
    }

    DelRemoteLink(conn);

    // erase link
    if (!conn->to.empty()) {
        if (conn->isRemote) {
            BUSLOG_DEBUG("remove remote link, to:{}", conn->to);
            (void)remoteLinks.erase(conn->to);
        } else {
            BUSLOG_DEBUG("remove local link, to:{}", conn->to);
            (void)links.erase(conn->to);
        }
    }

    if (conn->sendTotalLen != 0 && conn->sendMsgBase != nullptr) {
        delete conn->sendMsgBase;
        freeMsgNum++;
    }
    freeMsgNum += conn->sendQueue.size();
    MessageBase *tmpmsg = nullptr;
    while (!conn->sendQueue.empty()) {
        tmpmsg = conn->sendQueue.front();
        conn->sendQueue.pop();
        delete tmpmsg;
    }
    BUSLOG_DEBUG("close connection, fd:{},from:{},to:{},isRemote:{},free send msg num:{}", conn->fd, conn->from,
                 conn->to, conn->isRemote, freeMsgNum);
    if (conn->socketOperate != nullptr) {
        conn->socketOperate->Close(conn);
        delete conn->socketOperate;
    }

    if (conn->sendMetrics != nullptr) {
        delete conn->sendMetrics;
    }

    delete conn;
}

Connection *LinkMgr::FindLink(const std::string &to, bool remoteLink)
{
    Connection *conn = nullptr;
    if (!remoteLink) {
        auto iter = links.find(to);
        if (iter != links.end()) {
            conn = iter->second;
            return conn;
        }
    }
    auto iter = remoteLinks.find(to);
    if (iter != remoteLinks.end()) {
        conn = iter->second;
    }

    return conn;
}
Connection *LinkMgr::ExactFindLink(const std::string &to, bool remoteLink)
{
    Connection *conn = nullptr;
    if (!remoteLink) {
        auto iter = links.find(to);
        if (iter != links.end()) {
            conn = iter->second;
        }
    } else {
        auto iter = remoteLinks.find(to);
        if (iter != remoteLinks.end()) {
            conn = iter->second;
        }
    }
    return conn;
}

Connection *LinkMgr::FindLink(const std::string &to, bool remoteLink, bool exactNotRemote)
{
    if (exactNotRemote) {
        return ExactFindLink(to, false);
    } else {
        return FindLink(to, remoteLink);
    }
}

void LinkMgr::RefreshMetrics()
{
    for (const auto &iter : links) {
        iter.second->sendMetrics->Refresh();
    }
    for (const auto &iter : remoteLinks) {
        iter.second->sendMetrics->Refresh();
    }
}

Connection *LinkMgr::FindMaxLink()
{
    Connection *conn = nullptr;
    int count = 0;
    for (const auto &iter : links) {
        if (iter.second->sendMetrics->sendSum > count) {
            count = iter.second->sendMetrics->sendSum;
            conn = iter.second;
        }
    }
    for (const auto &iter : remoteLinks) {
        if (iter.second->sendMetrics->sendSum > count) {
            count = iter.second->sendMetrics->sendSum;
            conn = iter.second;
        }
    }
    return conn;
}

Connection *LinkMgr::FindFastLink()
{
    Connection *conn = nullptr;
    int size = 0;
    for (const auto &iter : links) {
        if (iter.second->sendMetrics->sendMaxSize > size) {
            size = iter.second->sendMetrics->sendMaxSize;
            conn = iter.second;
        }
    }
    for (const auto &iter : remoteLinks) {
        if (iter.second->sendMetrics->sendMaxSize > size) {
            size = iter.second->sendMetrics->sendMaxSize;
            conn = iter.second;
        }
    }
    return conn;
}

void LinkMgr::ExactDeleteLink(const std::string &to, bool remoteLink)
{
    Connection *conn = ExactFindLink(to, remoteLink);
    if (conn != nullptr) {
        BUSLOG_INFO("unLink, fd:{},to:{},remote:{}", conn->fd, to, remoteLink);
        CloseConnection(conn);
    } else {
        BUSLOG_DEBUG("link not found, to:{},remote:{}", to, remoteLink);
    }
}

void LinkMgr::DeleteAllLink(std::map<std::string, Connection *> &_links)
{
    auto iter = _links.begin();
    while (iter != _links.end()) {
        Connection *conn = iter->second;
        // erase link
        if (conn->recvMsgBase != nullptr) {
            delete conn->recvMsgBase;
        }
        iter = _links.erase(iter);

        BUSLOG_DEBUG("connection, fd:{},from:{},to:{},isRemote:{}", conn->fd, conn->from, conn->to, conn->isRemote);
        delete conn;
    }
}

void LinkMgr::AddLink(Connection *conn)
{
    if (conn == nullptr) {
        return;
    }
    Connection *tmpConn = ExactFindLink(conn->to, conn->isRemote);
    if (tmpConn != nullptr && tmpConn->isRemote == conn->isRemote) {
        BUSLOG_INFO("unLink, fd:{},to:{}", tmpConn->fd, tmpConn->to);
        CloseConnection(tmpConn);
    }

    if (conn->isRemote) {
        (void)remoteLinks.emplace(conn->to, conn);
    } else {
        (void)links.emplace(conn->to, conn);
    }
}

void LinkMgr::AddRemoteLink(Connection *conn)
{
    auto iter = allRemoteLinks.find(conn->fd);
    if (iter != allRemoteLinks.end()) {
        return;
    }
    (void)allRemoteLinks.emplace(conn->fd, conn);
}

void LinkMgr::DelRemoteLink(const Connection *conn)
{
    auto iter = allRemoteLinks.find(conn->fd);
    if (iter != allRemoteLinks.end()) {
        (void)allRemoteLinks.erase(iter);
    }

    if (!conn->timeoutRemoved && conn->isRemote &&
        (conn->recvMsgType == ParseType::KHTTP_RSP || conn->recvMsgType == ParseType::KHTTP_REQ)) {
        iter = httpRemoteLinks.find(conn->fd);
        if (iter != httpRemoteLinks.end()) {
            (void)httpRemoteLinks.erase(iter);
        }
    }
}

int LinkMgr::GetRemoteLinkCount() const
{
    return allRemoteLinks.size();
}

void LinkMgr::AddHttpRemoteLink(Connection *conn)
{
    auto iter = httpRemoteLinks.find(conn->fd);
    if (iter != httpRemoteLinks.end()) {
        return;
    }
    (void)httpRemoteLinks.emplace(conn->fd, conn);
}

void LinkMgr::SetLinkPriority(const std::string &to, bool remoteLink, ConnectionPriority pri)
{
    Connection *conn = ExactFindLink(to, remoteLink);
    if (conn != nullptr && conn->isRemote == remoteLink) {
        conn->priority = pri;
    }
}

void LinkMgr::DeleteLinker(int fd)
{
    auto iter = linkers.find(fd);
    if (iter == linkers.end()) {
        BUSLOG_DEBUG("not found linker,fd:{}", fd);
        return;
    }
    BUSLOG_DEBUG("erase linker, fd:{}", fd);
    auto _linkers = iter->second;
    auto iter2 = _linkers.begin();
    while (iter2 != _linkers.end()) {
        auto linkInfo = *iter2;
        if (linkInfo->delcb) {
            linkInfo->delcb(linkInfo->to, linkInfo->from);
        }
        iter2 = _linkers.erase(iter2);
        delete linkInfo;
    }
    (void)linkers.erase(fd);
}

void LinkMgr::DeleteLinker(const std::string &to, int fd)
{
    // if we run in double link pattern, link fd and send fd must be the same, send Exit message bind on this fd
    if (doubleLink) {
        DeleteLinker(fd);
        return;
    }

    // if we run in single link pattern, link fd and send fd may not be the same, we should send Exit message bind
    // on link fd and remote link fd. Here 'isExited' flag should be set true to avoid duplicate Exit message with
    // same aid.
    Connection *nonRemoteConn = LinkMgr::ExactFindLink(to, false);
    if (nonRemoteConn != nullptr) {
        nonRemoteConn->isExited = true;
        DeleteLinker(nonRemoteConn->fd);
        if (nonRemoteConn->fd != fd) {
            BUSLOG_INFO("delete linker bind on link fd, fd:{},delete fd:{}", nonRemoteConn->fd, fd);
        }
    }

    Connection *remoteConn = LinkMgr::ExactFindLink(to, true);
    if (remoteConn != nullptr) {
        remoteConn->isExited = true;
        DeleteLinker(remoteConn->fd);
        if (remoteConn->fd != fd) {
            BUSLOG_INFO("delete linker bind on remote link fd, fd:{},delete fd:{}", remoteConn->fd, fd);
        }
    }
}

void LinkMgr::DeleteAllLinker()
{
    auto iter = linkers.begin();
    while (iter != linkers.end()) {
        int fd = iter->first;
        BUSLOG_DEBUG("erase linker, fd:{}", fd);
        auto _linkers = iter->second;
        auto iter2 = _linkers.begin();
        while (iter2 != _linkers.end()) {
            auto linkInfo = *iter2;
            iter2 = _linkers.erase(iter2);
            delete linkInfo;
        }
        iter = linkers.erase(iter);
    }
}

LinkerInfo *LinkMgr::FindLinker(int fd, const AID &sAid, const AID &dAid)
{
    auto iter = linkers.find(fd);
    if (iter == linkers.end()) {
        return nullptr;
    }
    auto _linkers = iter->second;
    auto iter2 = _linkers.begin();
    while (iter2 != _linkers.end()) {
        auto linkInfo = *iter2;
        if (linkInfo->from == sAid && linkInfo->to == dAid) {
            return linkInfo;
        }
        ++iter2;
    }
    return nullptr;
}

void LinkMgr::AddLinker(int fd, const AID &sAid, const AID &dAid, LinkerCallBack delcb)
{
    LinkerInfo *linker = FindLinker(fd, sAid, dAid);
    if (linker != nullptr) {
        // find
        return;
    }
    linker = new (std::nothrow) LinkerInfo();
    if (linker == nullptr) {
        BUSLOG_ERROR("new LinkerInfo fail, sAid:{},dAid:{}", std::string(sAid), std::string(dAid));
        return;
    }
    linker->from = sAid;
    linker->to = dAid;
    linker->fd = fd;
    linker->delcb = delcb;
    BUSLOG_DEBUG("add linker, fd:{}", fd);
    (void)linkers[fd].insert(linker);
}

bool LinkMgr::SwapLinkerSocket(int fromFd, int toFd)
{
    auto iter = linkers.find(fromFd);
    if (iter == linkers.end()) {
        // not found
        return false;
    }
    auto _linkers = iter->second;
    (void)linkers.erase(fromFd);
    linkers[toFd] = _linkers;
    return true;
}

LinkMgr::~LinkMgr()
{
    try {
        httpRemoteLinks.clear();
        allRemoteLinks.clear();
        DeleteAllLink(links);
        DeleteAllLink(remoteLinks);
        DeleteAllLinker();
    } catch (...) {
        // Ignore
    }
}

LinkMgr *LinkMgr::GetLinkMgr()
{
    return LinkMgr::linkMgr;
}
void LinkMgr::SetLinkMgr(LinkMgr *linkmgr)
{
    LinkMgr::linkMgr = linkmgr;
}

void ConnectionUtil::SetSocketOperate(Connection *conn)
{
    if (conn->socketOperate != nullptr) {
        return;
    }
#ifdef SSL_ENABLED

    if (openssl::IsSslEnabled()) {
        conn->socketOperate = new (std::nothrow) SSLSocketOperate();
        conn->type = ConnectionType::TYPE_SSL;
    } else {
        conn->socketOperate = new (std::nothrow) TCPSocketOperate();
    }
#else
    conn->socketOperate = new (std::nothrow) TCPSocketOperate();
#endif
    BUS_OOM_EXIT(conn->socketOperate);
}

bool ConnectionUtil::ParseHeader(Connection *conn, uint32_t &recvLen, int &retval)
{
    std::string magicID = "";
    char *recvBuf = reinterpret_cast<char *>(&conn->recvHeader) + conn->recvLen;
    retval = conn->socketOperate->Recv(conn, recvBuf, sizeof(MsgHeader) - conn->recvLen, recvLen);
    if (retval < 0) {
        conn->connState = ConnectionState::DISCONNECTING;
        conn->recvLen += recvLen;
        return false;
    }
    if ((recvLen + conn->recvLen) != sizeof(MsgHeader)) {
        conn->recvLen += recvLen;
        return false;
    }
    conn->recvLen = 0;

    if (strncmp(conn->recvHeader.magic, BUS_MAGICID.c_str(), BUS_MAGICID.size()) != 0) {
        BUSLOG_ERROR("check magicid fail, BUS_MAGICID:{},recv magicID:{}", BUS_MAGICID, magicID);
        conn->connState = ConnectionState::DISCONNECTING;
        return false;
    }
    EvbufMgr::HeaderNtoH(&conn->recvHeader);

    BUSLOG_DEBUG("recvmsg]nameLen:{},toLen:{},fromLen:{},signatureLen:{},bodyLen:{}", conn->recvHeader.nameLen,
                 conn->recvHeader.toLen, conn->recvHeader.fromLen, conn->recvHeader.signatureLen,
                 conn->recvHeader.bodyLen);

    EvbufMgr::PrepareRecvMsg(conn);
    if (conn->connState == ConnectionState::DISCONNECTING) {
        return false;
    }
    return true;
}

// parse message from buffer.
bool ConnectionUtil::Parse(int, Connection *conn)
{
    int retval;
    uint32_t recvLen;

    // run state machine
    switch (conn->recvState) {
        case State::MSG_HEADER:
            if (!ParseHeader(conn, recvLen, retval)) {
                return false;
            }
            conn->recvState = State::BODY;

        /* fall through */
        case State::BODY:
            retval = conn->socketOperate->Recvmsg(conn, &conn->recvMsg, conn->recvTotalLen);
            if (retval != static_cast<int>(conn->recvTotalLen)) {
                if (retval < 0) {
                    conn->connState = ConnectionState::DISCONNECTING;
                    return false;
                }
                conn->recvTotalLen -= static_cast<unsigned int>(retval);
                return false;
            }

            BUSLOG_DEBUG("recvmsg]recv name:{}, from:{}, conn fd:{}, remote:{}, peer:{}", conn->recvMsgBase->name,
                         conn->recvFrom, conn->fd, conn->isRemote, conn->peer);
            conn->recvMsgBase->peer = conn->isRemote ? conn->peer : conn->to;
            conn->recvMsgBase->SetFrom(conn->recvFrom);
            conn->recvMsgBase->SetTo(conn->recvTo);
            conn->recvState = State::MSG_HEADER;
            break;
        default:
            return false;
            /* fall through */
    }    // end switch

    return true;
}

int ConnectionUtil::RecvKMsg(Connection *conn, IOMgr::MsgHandler msgHandler)
{
    bool ok = Parse(conn->fd, conn);
    // if no message parsed, wait for next read
    if (!ok) {
        BUSLOG_DEBUG("no message parsed,wait for next read, fd:{},recvState:{}", conn->fd,
                     static_cast<std::underlying_type_t<State>>(conn->recvState));
        if (conn->connState == ConnectionState::DISCONNECTING) {
            return -1;
        }
        return 0;
    }

    if (conn->recvMsgBase == nullptr) {
        BUSLOG_ERROR("received an empty message");
        conn->connState = ConnectionState::DISCONNECTING;
        return -1;
    }

    if (!conn->recvMsgBase->from.OK() || !conn->recvMsgBase->to.OK()) {
        BUSLOG_ERROR("from/to is invalid, from:{},to:{}", std::string(conn->recvMsgBase->from),
                     std::string(conn->recvMsgBase->to));
        conn->connState = ConnectionState::DISCONNECTING;
        return -1;
    }

    if (conn->to.empty()) {
        // remote link
        std::string fromUrl = conn->recvMsgBase->from;
        size_t index = fromUrl.find("@");
        if (index != std::string::npos) {
            conn->to = fromUrl.substr(index + 1);
            BUSLOG_INFO("new conn, fd:{},to:{}", conn->fd, conn->to);
            std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
            LinkMgr::GetLinkMgr()->SetLinkPriority(conn->to, false, ConnectionPriority::PRI_LOW);
            conn->connState = ConnectionState::CONNECTED;
            LinkMgr::GetLinkMgr()->AddLink(conn);
        }
    }
    std::unique_ptr<MessageBase> msg(conn->recvMsgBase);
    conn->recvMsgBase = nullptr;

    // call msg handler if set
    if (msgHandler != nullptr) {
        msgHandler(std::move(msg));
    } else {
        BUSLOG_INFO("mshandler was not found");
    }

    return 1;
}

void ConnectionUtil::CheckRecvMsgType(Connection *conn)
{
    std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
    if (conn->recvMsgType != ParseType::UNKNOWN) {
        return;
    }

    std::string magicID = "";
    magicID.resize(BUS_MAGICID.size());
    char *buf = const_cast<char *>(magicID.data());

    int size = conn->socketOperate->RecvPeek(conn, buf, BUS_MAGICID.size());
    if (size < static_cast<int>(BUS_MAGICID.size())) {
        if (size == 0) {
            BUSLOG_DEBUG("recvmsg, fd:{},size:{},magicSize:{},errno:{}", conn->fd, size,
                         static_cast<int>(BUS_MAGICID.size()), errno);
            conn->connState = ConnectionState::DISCONNECTING;
        }
        return;
    }

    if (BUS_MAGICID == magicID) {
        conn->recvState = State::MSG_HEADER;
        conn->recvMsgType = ParseType::KMSG;
#ifdef HTTP_ENABLED
    } else {
        conn->recvMsgType = ("HTTP" == magicID) ? ParseType::KHTTP_RSP : ParseType::KHTTP_REQ;
        if (conn->isRemote) {
            LinkMgr::GetLinkMgr()->AddHttpRemoteLink(conn);
        }
#endif
    }
    return;
}

void ConnectionUtil::SocketEventHandler(int fd, uint32_t events, void *context)
{
    Connection *conn = static_cast<Connection *>(context);

    if (fd != conn->fd) {
        BUSLOG_ERROR("conn invalid reuse,del & close fd, fd:{},connfd:{},event:{}", fd, conn->fd, events);

        (void)conn->recvEvloop->DelFdEvent(fd);
        conn->connState = ConnectionState::DISCONNECTING;
        if (conn->eventCallBack != nullptr) {
            conn->eventCallBack(conn);
        } else {
            // exit ?
            BUSLOG_ERROR("eventCallBack is null, fd={},events={}", fd, events);
        }

        return;
    }
    if (events & static_cast<unsigned int>(EPOLLOUT)) {
        (void)conn->recvEvloop->ModifyFdEvent(fd, static_cast<unsigned int>(EPOLLIN) |
                                                      static_cast<unsigned int>(EPOLLHUP) |
                                                      static_cast<unsigned int>(EPOLLERR));
        if (conn->writeCallBack != nullptr) {
            conn->writeCallBack(conn);
        }
    }

    if (events & static_cast<unsigned int>(EPOLLIN)) {
        if (conn->readCallBack != nullptr) {
            conn->readCallBack(conn);
        }
    }

    if (conn->connState == ConnectionState::DISCONNECTING ||
        (conn->recvMsgType != ParseType::KHTTP_REQ && conn->recvMsgType != ParseType::KHTTP_RSP &&
         (events &
          (static_cast<uint32_t>(EPOLLHUP) | static_cast<uint32_t>(EPOLLRDHUP) | static_cast<uint32_t>(EPOLLERR))))) {
        if (conn->recvMsgType == ParseType::KMSG) {
            BUSLOG_INFO("event value, fd:{},events:{},state:{},errcode:{},errno:{},to:{},type:{},remote:{}", fd,
                        events, static_cast<std::underlying_type_t<ConnectionState>>(conn->connState), conn->errCode,
                        errno, conn->to, static_cast<std::underlying_type_t<ParseType>>(conn->recvMsgType),
                        conn->isRemote);
        } else {
            BUSLOG_DEBUG("event value, fd:{},events:{},state:{},errcode:{},errno:{},to:{},type:{},remote:{}", fd,
                         events, static_cast<std::underlying_type_t<ConnectionState>>(conn->connState), conn->errCode,
                         errno, conn->to, static_cast<std::underlying_type_t<ParseType>>(conn->recvMsgType),
                         conn->isRemote);
        }

        conn->connState = ConnectionState::DISCONNECTING;
        if (conn->eventCallBack != nullptr) {
            conn->eventCallBack(conn);
        } else {
            BUSLOG_ERROR("eventCallBack is null, fd={},events={}", fd, events);
        }
    }
}

int ConnectionUtil::AddSockEventHandler(Connection *conn)
{
    /* add to epoll */
    return conn->recvEvloop->AddFdEvent(conn->fd,
                                        static_cast<unsigned int>(EPOLLIN) | static_cast<unsigned int>(EPOLLHUP) |
                                            static_cast<unsigned int>(EPOLLRDHUP) |
                                            static_cast<unsigned int>(EPOLLERR),
                                        ConnectionUtil::SocketEventHandler, static_cast<void *>(conn));
}

bool ConnectionUtil::ConnEstablishedDelAdd(Connection *conn, int fd, uint32_t events, int *soError, uint32_t error)
{
    int retval;
    socklen_t len = sizeof(*soError);

    retval = conn->recvEvloop->DelFdEvent(fd);
    if (retval) {
        BUSLOG_ERROR("DelFd fail, fd:{},ev:{}", fd, events);
        return false;
    }
    retval = getsockopt(fd, SOL_SOCKET, SO_ERROR, soError, &len);
    if (retval) {
        BUSLOG_DEBUG("getsockopt fail, fd:{},events:{},errno:{}", fd, events, errno);
        *soError = errno;
    }
    if (*soError || error) {
        BUSLOG_DEBUG("onn establish fail, fd:{},events:{},soError:{},epollError:{}", fd, events, *soError, error);
        return false;
    }
    retval = ConnectionUtil::AddSockEventHandler(conn);
    if (retval != BUS_OK) {
        BUSLOG_ERROR("AddSockEventHandler fail, fd={},events={}", fd, events);
        return false;
    }

    return true;
}

int ConnectionUtil::AddNewConnEventHandler(Connection *conn)
{
    return conn->recvEvloop->AddFdEvent(
        conn->fd,
        static_cast<unsigned int>(EPOLLIN) | static_cast<unsigned int>(EPOLLHUP) | static_cast<unsigned int>(EPOLLERR),
        NewConnEventHandler, static_cast<void *>(conn));
}

void ConnectionUtil::CleanUp(int fd, Connection *conn)
{
    if (LOG_CHECK_EVERY_N()) {
        BUSLOG_INFO("new con fail, fd:{},state:{},errno:{},to:{},type:{}", fd,
                    static_cast<std::underlying_type_t<ConnectionState>>(conn->connState), errno, conn->to,
                    static_cast<std::underlying_type_t<ParseType>>(conn->recvMsgType));
    } else {
        BUSLOG_DEBUG("new con fail, fd:{},state:{},errno:{},to:{},type:{}", fd,
                     static_cast<std::underlying_type_t<ConnectionState>>(conn->connState), errno, conn->to,
                     static_cast<std::underlying_type_t<ParseType>>(conn->recvMsgType));
    }

    conn->connState = ConnectionState::DISCONNECTING;
    conn->eventCallBack(conn);

    return;
}

void ConnectionUtil::NewConnEventHandler(int fd, uint32_t events, void *context)
{
    int retval;
    Connection *conn = static_cast<Connection *>(context);
    conn->socketOperate->NewConnEventHandler(fd, events, context);

    if (conn->connState == ConnectionState::DISCONNECTING) {
        CleanUp(fd, conn);
        return;
    } else if (conn->connState != ConnectionState::CONNECTED) {
        // The handshake is not complete
        return;
    }

    /* remove from epoll */
    retval = conn->recvEvloop->DelFdEvent(fd);
    if (retval) {
        BUSLOG_ERROR("epoll remove connect handler fail, fd:{}", fd);
        return;
    }

    retval = ConnectionUtil::AddSockEventHandler(conn);
    if (retval != BUS_OK) {
        BUSLOG_ERROR("AddSockEventHandler fail, fd:{},events:{}", fd, events);
        CleanUp(fd, conn);
        return;
    }

    conn->writeCallBack(conn);

    ConnectionUtil::SocketEventHandler(fd, events, context);

    return;
}

void ConnectionUtil::CloseConnection(Connection *conn)
{
    std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
    LinkMgr::GetLinkMgr()->CloseConnection(conn);
}

Connection::Connection()
    : fd(-1),
      isRemote(false),
      isExited(false),
      type(ConnectionType::TYPE_TCP),
      recvState(MSG_HEADER),
      recvLen(0),
      recvMsgType(UNKNOWN),
      connState(INIT),
      errCode(0),
      priority(ConnectionPriority::PRI_HIGH)
{
    InitMsgHeader(recvHeader);
    InitMsgHeader(sendHeader);
    recvMsg.msg_control = nullptr;
    recvMsg.msg_controllen = 0;
    recvMsg.msg_flags = 0;
    recvMsg.msg_name = nullptr;
    recvMsg.msg_namelen = 0;
    recvMsg.msg_iov = recvIov;
    recvMsg.msg_iovlen = RECVMSG_IOVLEN;
    recvMsgBase = nullptr;

    sendMsg.msg_control = nullptr;
    sendMsg.msg_controllen = 0;
    sendMsg.msg_flags = 0;
    sendMsg.msg_name = nullptr;
    sendMsg.msg_namelen = 0;
    sendMsg.msg_iov = sendIov;
    sendMsg.msg_iovlen = SENDMSG_IOVLEN;
}
Connection::~Connection()
{
    if (recvEvloop != nullptr) {
        recvEvloop = nullptr;
    }
    if (socketOperate != nullptr) {
        socketOperate = nullptr;
    }
    if (sendMsgBase != nullptr) {
        sendMsgBase = nullptr;
    }
    if (sendMetrics != nullptr) {
        sendMetrics = nullptr;
    }
    if (eventCallBack != nullptr) {
        eventCallBack = nullptr;
    }
    if (writeCallBack != nullptr) {
        writeCallBack = nullptr;
    }
    if (sendEvloop != nullptr) {
        sendEvloop = nullptr;
    }
    if (recvMsgBase != nullptr) {
        recvMsgBase = nullptr;
    }
    if (readCallBack != nullptr) {
        readCallBack = nullptr;
    }

#ifdef HTTP_ENABLED
    if (decoder != nullptr) {
        delete decoder;
        decoder = nullptr;
    };
#endif
}

}    // namespace litebus
