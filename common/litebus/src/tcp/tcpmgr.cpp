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

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "tcp_socket.hpp"

#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"

#include "ssl/ssl_socket.hpp"

#endif

#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"
#include "actor/sysmgr_actor.hpp"
#include "litebus.hpp"
#include "utils/os_utils.hpp"

#include "tcpmgr.hpp"

namespace litebus {

constexpr size_t MAX_ENV_BOOLEAN_LENGTH = litebus::os::ENV_VAR_MAX_LENGTH;

constexpr auto MAX_RECV_COUNT = 3;
constexpr auto MAX_RECYCLE_LINK_COUNT = 10;
constexpr auto MAX_REMOTE_LINK_COUNT_DEFAULT = 20000;
constexpr auto MAX_REMOTE_LINK_COUNT_MIN = 10000;
constexpr auto MAX_REMOTE_LINK_COUNT_MAX = 50000;
#ifdef HTTP_ENABLED
RecvCallBack TCPMgr::httpReqCb = nullptr;
RecvCallBack TCPMgr::httpRspCb = nullptr;
CheckConCallBack TCPMgr::httpConCheckCb = nullptr;
#endif
std::string TCPMgr::advertiseUrl = "";
bool TCPMgr::isHttpKmsg = false;
uint64_t TCPMgr::outTcpBufSize = 0;
IOMgr::MsgHandler TCPMgr::tcpMsgHandler;
int TCPMgr::maxRemoteLinkCount = MAX_REMOTE_LINK_COUNT_DEFAULT;

namespace tcpUtil {
int DoConnect(const std::string &to, Connection *conn, ConnectionCallBack eventCallBack,
              ConnectionCallBack writeCallBack, ConnectionCallBack readCallBack)
{
    IOSockaddr addr;

    if (!SocketOperate::GetSockAddr(to, addr)) {
        return -1;
    }

    int fd = SocketOperate::CreateSocket(addr.sa.sa_family);
    if (fd < 0) {
        return -1;
    }

    if (conn == nullptr) {
        return -1;
    }

    conn->fd = fd;
    conn->eventCallBack = eventCallBack;
    conn->writeCallBack = writeCallBack;
    conn->readCallBack = readCallBack;

    int ret = TCPMgr::TcpConnect(conn, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        BUSLOG_DEBUG("socket connect fail, fd:{},to:{}", fd, to);
        (void)close(fd);
        conn->fd = -1;
        return -1;
    }

    BUSLOG_DEBUG("wait peer ack, conSeq={},fd:{},to:{}", conn->sequence, fd, to);
    return 0;
}

void CleanUp(int fd, Connection *conn, uint32_t error, int soError)
{
    if (LOG_CHECK_EVERY_N()) {
        BUSLOG_INFO("connect fail, fd:{},to:{},events:{},errno:{}", fd, conn->to, error, soError);
    } else {
        BUSLOG_DEBUG("connect fail, fd:{},to:{},events:{},errno:{}", fd, conn->to, error, soError);
    }

    conn->connState = ConnectionState::DISCONNECTING;
    conn->errCode = soError;
    conn->eventCallBack(conn);
    return;
}

void ConnEstablishedEvHandler(int fd, uint32_t events, void *context)
{
    uint32_t error = events & (static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLHUP) |
                               static_cast<unsigned int>(EPOLLRDHUP));
    int soError = 0;
    Connection *conn = static_cast<Connection *>(context);
    conn->socketOperate->ConnEstablishedEventHandler(fd, events, context);
    if (conn->connState == ConnectionState::DISCONNECTING) {
        CleanUp(fd, conn, error, soError);
        return;
    } else if (conn->connState != ConnectionState::CONNECTED) {
        return;
    }

    if (!ConnectionUtil::ConnEstablishedDelAdd(conn, fd, events, &soError, error)) {
        CleanUp(fd, conn, error, soError);
        return;
    }
    BUSLOG_DEBUG("connect succ, fd:{},to:{}", "", fd, conn->to);
    if (conn->writeCallBack) {
        conn->writeCallBack(conn);
    }
    return;
}

void OnAccept(int server, uint32_t events, void *arg)
{
    if (events & (static_cast<unsigned int>(EPOLLHUP) | static_cast<unsigned int>(EPOLLERR))) {
        BUSLOG_ERROR("error event, serverfd:{},events:{}", server, events);
        return;
    }
    TCPMgr *tcpmgr = static_cast<TCPMgr *>(arg);
    if (tcpmgr->recvEvloop == nullptr) {
        BUSLOG_ERROR("EvLoop is null, serverfd:{},events:{}", server, events);
        return;
    }

    // accept connection
    auto acceptFd = SocketOperate::Accept(server);
    if (acceptFd < 0) {
        BUSLOG_ERROR("accept fail, serverfd:{},events:{}", server, events);
        return;
    }

    if (LinkMgr::GetLinkMgr()->GetRemoteLinkCount() >= TCPMgr::maxRemoteLinkCount) {
        BUSLOG_ERROR("remote link overrun, serverfd:{},events:{},acceptFd:{}", server, events, acceptFd);
        (void)close(acceptFd);
        acceptFd = -1;
        return;
    }

    Connection *conn = tcpmgr->CreateRemoteConn(acceptFd);
    if (conn == nullptr) {
        BUSLOG_ERROR("new Connection fail, serverfd:{},events:{},acceptFd:{}", server, events, acceptFd);
        return;
    }

    ConnectionUtil::SetSocketOperate(conn);

    // register read event callback for server socket
    int retval = ConnectionUtil::AddNewConnEventHandler(conn);
    if (retval != BUS_OK) {
        BUSLOG_ERROR("add acceptFd event fail, serverfd:{},events:{},acceptFd:{}", server, events, acceptFd);
        (void)close(acceptFd);
        acceptFd = -1;
        delete conn->sendMetrics;
        delete conn;
        return;
    }
    LinkMgr::GetLinkMgr()->AddRemoteLink(conn);
}

void ConnectionSend(Connection *conn)
{
    conn->noCommTime = 0;
    while (!conn->sendQueue.empty() || conn->sendTotalLen != 0) {
        if (conn->sendTotalLen == 0) {
            EvbufMgr::PrepareSendMsg(conn, conn->sendQueue.front(), TCPMgr::GetAdvertiseUrl(), TCPMgr::IsHttpKmsg());
            conn->sendQueue.pop();
        }

        int sendLen = conn->socketOperate->Sendmsg(conn, &conn->sendMsg, conn->sendTotalLen);
        if (sendLen > 0) {
            if (conn->sendTotalLen == 0) {
                BUSLOG_DEBUG("send succ, to:{}", conn->to);
                // update metrics
                conn->sendMetrics->UpdateError(false);

                TCPMgr::SetTCPOutSize(TCPMgr::GetTCPOutSize() - conn->sendMsgBase->body.size());
                conn->outBufferSize -= conn->sendMsgBase->body.size();
                delete conn->sendMsgBase;
                conn->sendMsgBase = nullptr;
            }
        } else if (sendLen == 0) {
            // EAGAIN
            (void)conn->recvEvloop->ModifyFdEvent(
                conn->fd, static_cast<unsigned int>(EPOLLOUT) | static_cast<unsigned int>(EPOLLIN) |
                              static_cast<unsigned int>(EPOLLHUP) | static_cast<unsigned int>(EPOLLERR));
            break;
        } else {
            BUSLOG_DEBUG("send fail, to:{}", conn->to);
            // update metrics
            conn->sendMetrics->UpdateError(true, conn->errCode);

            conn->connState = ConnectionState::DISCONNECTING;
            break;
        }
    }
}

}    // namespace tcpUtil

TCPMgr::~TCPMgr()
{
    try {
        FinishDestruct();
    } catch (...) {
        // Ignore
    }
}

void TCPMgr::SendExitMsg(const std::string &from, const std::string &to)
{
    if (tcpMsgHandler != nullptr) {
        std::unique_ptr<MessageBase> exitMsg(new (std::nothrow) MessageBase(MessageBase::Type::KEXIT));
        BUS_OOM_EXIT(exitMsg);

        exitMsg->SetFrom(from);
        exitMsg->SetTo(to);

        BUSLOG_DEBUG("exit msg, from:{},to:{}", from, to);
        // msg owner transferred to msg_handler
        tcpMsgHandler(std::move(exitMsg));
    }
}

void TCPMgr::RegisterMsgHandle(IOMgr::MsgHandler handler)
{
    tcpMsgHandler = handler;
}

void TCPMgr::InitRemoteLinkMaxSetting()
{
    char *remoteLinkMaxEnv = getenv("LITEBUS_REMOTE_LINK_MAX");
    int count = MAX_REMOTE_LINK_COUNT_DEFAULT;
    if (remoteLinkMaxEnv != nullptr) {
        try {
            count = std::stoi(remoteLinkMaxEnv);
        } catch (const std::exception &e) {
            BUSLOG_ERROR("stoi fail:{}, error:{} ", remoteLinkMaxEnv, e.what());
            count = MAX_REMOTE_LINK_COUNT_DEFAULT;
        }
        if (count < MAX_REMOTE_LINK_COUNT_MIN || count > MAX_REMOTE_LINK_COUNT_MAX) {
            count = MAX_REMOTE_LINK_COUNT_DEFAULT;
        }
    }
    BUSLOG_INFO("remote link max set:{}", count);
    maxRemoteLinkCount = count;
}

bool TCPMgr::Init()
{
    LinkMgr::SetLinkMgr(new (std::nothrow) LinkMgr());
    if (LinkMgr::GetLinkMgr() == nullptr) {
        BUSLOG_ERROR("new LinkMgr failed");
        return false;
    }
    recvEvloop = new (std::nothrow) EvLoop();
    if (recvEvloop == nullptr) {
        BUSLOG_ERROR("new recv evLoop failed");
        return false;
    }

    bool ok = recvEvloop->Init(TCP_RECV_EVLOOP_THREADNAME);
    if (!ok) {
        BUSLOG_ERROR("recv evLoop init failed");
        delete recvEvloop;
        recvEvloop = nullptr;
        return false;
    }

    sendEvloop = new (std::nothrow) EvLoop();
    if (sendEvloop == nullptr) {
        BUSLOG_ERROR("new send evLoop failed");
        delete recvEvloop;
        recvEvloop = nullptr;
        return false;
    }
    ok = sendEvloop->Init(TCP_SEND_EVLOOP_THREADNAME);
    if (!ok) {
        BUSLOG_ERROR("send evLoop init failed");
        delete recvEvloop;
        recvEvloop = nullptr;
        delete sendEvloop;
        sendEvloop = nullptr;
        return false;
    }

    if (litebus::GetHttpKmsgFlag() < 0) {
        char *httpKmsgEnv = getenv("LITEBUS_HTTPKMSG_ENABLED");
        if (httpKmsgEnv != nullptr && strlen(httpKmsgEnv) <= MAX_ENV_BOOLEAN_LENGTH &&
            (std::string(httpKmsgEnv) == "true" || std::string(httpKmsgEnv) == "1")) {
            isHttpKmsg = true;
        }
    } else {
        isHttpKmsg = (litebus::GetHttpKmsgFlag() == 0) ? false : true;
    }

    LinkMgr::GetLinkMgr()->SetLinkPattern(isHttpKmsg);
    BUSLOG_INFO("init succ, LITEBUS_HTTPKMSG_ENABLED:{}", isHttpKmsg);

    InitRemoteLinkMaxSetting();

    return true;
}

bool TCPMgr::StartIOServer(const std::string &url, const std::string &aAdvertiseUrl)
{
    // create server socket
    serverFd = SocketOperate::Listen(url);
    if (serverFd < 0) {
        BUSLOG_ERROR("listen fail, url:{},advertiseUrl:{}", url, advertiseUrl);
        return false;
    }

    url_ = url;
    advertiseUrl = aAdvertiseUrl;

    if (advertiseUrl.empty()) {
        advertiseUrl = url_;
    }

    size_t index = url.find(URL_PROTOCOL_IP_SEPARATOR);
    if (index != std::string::npos) {
        url_ = url.substr(index + URL_PROTOCOL_IP_SEPARATOR.length());
    }

    index = advertiseUrl.find(URL_PROTOCOL_IP_SEPARATOR);
    if (index != std::string::npos) {
        advertiseUrl = advertiseUrl.substr(index + URL_PROTOCOL_IP_SEPARATOR.length());
    }

    // register read event callback for server socket
    int retval = recvEvloop->AddFdEvent(
        serverFd,
        static_cast<unsigned int>(EPOLLIN) | static_cast<unsigned int>(EPOLLHUP) | static_cast<unsigned int>(EPOLLERR),
        tcpUtil::OnAccept, static_cast<void *>(this));
    if (retval != BUS_OK) {
        BUSLOG_ERROR("add server event fail, url:{},advertiseUrl:{}", url, advertiseUrl);
        return false;
    }

    BUSLOG_INFO("start server succ, fd:{},url:{},advertiseUrl:{}", serverFd, url, advertiseUrl);

    return true;
}

void TCPMgr::ReadCallBack(void *context)
{
    Connection *conn = static_cast<Connection *>(context);
    int count = 0;
    int retval;

    do {
        retval = RecvMsg(conn);
        ++count;
    } while (retval > 0 && count < MAX_RECV_COUNT);

    return;
}

void TCPMgr::EventCallBack(void *context)
{
    Connection *conn = static_cast<Connection *>(context);

    if (conn->connState == ConnectionState::CONNECTED) {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        tcpUtil::ConnectionSend(conn);
    } else if (conn->connState == ConnectionState::DISCONNECTING) {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        outTcpBufSize -= conn->outBufferSize;
        LinkMgr::GetLinkMgr()->CloseConnection(conn);
    }
}

void TCPMgr::WriteCallBack(void *context)
{
    Connection *conn = static_cast<Connection *>(context);
    if (conn->connState == ConnectionState::CONNECTED) {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        tcpUtil::ConnectionSend(conn);
    }
}

int TCPMgr::RecvMsg(Connection *conn)
{
    ConnectionUtil::CheckRecvMsgType(conn);

    switch (conn->recvMsgType) {
        case ParseType::KMSG:
            if (!isHttpKmsg) {
                return ConnectionUtil::RecvKMsg(conn, tcpMsgHandler);
            } else {
                conn->connState = ConnectionState::DISCONNECTING;
                return -1;
            }
#ifdef HTTP_ENABLED
        case ParseType::KHTTP_REQ:
            if (httpReqCb) {
                conn->noCommTime = 0;
                return httpReqCb(conn, tcpMsgHandler);
            } else {
                conn->connState = ConnectionState::DISCONNECTING;
                return -1;
            }
        case ParseType::KHTTP_RSP:
            if (httpRspCb) {
                conn->noCommTime = 0;
                return httpRspCb(conn, tcpMsgHandler);
            } else {
                conn->connState = ConnectionState::DISCONNECTING;
                return -1;
            }
#endif
        default:
            BUSLOG_DEBUG("fd:{},recvMsgType:{}", conn->fd,
                         static_cast<std::underlying_type_t<ParseType>>(conn->recvMsgType));
            return 0;
    }
}

bool TCPMgr::ConnEstablishedSSL(Connection *conn, int fd)
{
    if (LOG_CHECK_EVERY_N()) {
        BUSLOG_INFO("connect downgrade,to:{}", conn->to);
    } else {
        BUSLOG_DEBUG("connect downgrade,to:{}", conn->to);
    }

    (void)conn->recvEvloop->DelFdEvent(fd);
    conn->socketOperate->Close(conn);
    delete conn->socketOperate;

    conn->recvMsgType = UNKNOWN;
    conn->connState = INIT;
    conn->type = ConnectionType::TYPE_TCP;
    conn->socketOperate = new (std::nothrow) TCPSocketOperate();
    BUS_OOM_EXIT(conn->socketOperate);
    int ret = tcpUtil::DoConnect(conn->to, conn, TCPMgr::EventCallBack, TCPMgr::WriteCallBack, TCPMgr::ReadCallBack);
    if (ret < 0) {
        BUSLOG_INFO("fail to connect downgrade, to:{}", conn->to);
        return false;
    }

    return true;
}

int TCPMgr::AddConnEstablishedHandler(Connection *conn)
{
    /* add to epoll */
    return conn->recvEvloop->AddFdEvent(conn->fd,
                                        (static_cast<unsigned int>(EPOLLOUT) | static_cast<unsigned int>(EPOLLHUP) |
                                         static_cast<unsigned int>(EPOLLRDHUP) | static_cast<unsigned int>(EPOLLERR)),
                                        tcpUtil::ConnEstablishedEvHandler, static_cast<void *>(conn));
}

int TCPMgr::TcpConnect(Connection *conn, const struct sockaddr *sa, socklen_t saLen)
{
    int retval;
    uint16_t localPort;

    retval = SocketOperate::Connect(conn->fd, sa, saLen, localPort);
    if (retval != BUS_OK) {
        return BUS_ERROR;
    }

    // init metrics
    if (conn->sendMetrics == nullptr) {
        conn->sendMetrics = new (std::nothrow) SendMetrics();
        if (conn->sendMetrics == nullptr) {
            return BUS_ERROR;
        }
    }

    /* add to epoll */
    retval = AddConnEstablishedHandler(conn);
    if (retval != BUS_OK) {
        if (conn->sendMetrics != nullptr) {
            delete conn->sendMetrics;
            conn->sendMetrics = nullptr;
        }
        return BUS_ERROR;
    }

    return BUS_OK;
}

void TCPMgr::Send(MessageBase *msg, const TCPMgr *tcpmgr, bool remoteLink, bool isExactNotRemote)
{
    std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
    // search connection by the target address
    Connection *conn = LinkMgr::GetLinkMgr()->FindLink(msg->to.Url(), remoteLink, isExactNotRemote);
    if (conn == nullptr) {
        BUSLOG_DEBUG("send,not found link and to connect, from:{},to:{},remoteLink:{}", advertiseUrl, msg->to.Url(),
                     remoteLink);
        if (remoteLink && (!isExactNotRemote)) {
            BUSLOG_ERROR("send,not found remote link and send fail, name:{},from:{},to:{}", msg->name, advertiseUrl,
                         msg->to.Url());
            delete msg;
            return;
        }
        conn = tcpmgr->CreateSendMsgConn(msg);
        if (conn == nullptr) {
            return;
        }
        LinkMgr::GetLinkMgr()->AddLink(conn);
    }

    if (!conn->isRemote && !isExactNotRemote && conn->priority == ConnectionPriority::PRI_LOW) {
        Connection *remoteConn = LinkMgr::GetLinkMgr()->ExactFindLink(msg->to.Url(), true);
        if (remoteConn != nullptr && remoteConn->connState == ConnectionState::CONNECTED) {
            conn = remoteConn;
        }
    }

    BUSLOG_DEBUG("send msg,fd:{},name:{},from:{},to:{}", "", conn->fd, msg->name, advertiseUrl, msg->to.Url());
    if (conn->sendTotalLen == 0) {
        EvbufMgr::PrepareSendMsg(conn, msg, advertiseUrl, isHttpKmsg);
    } else {
        conn->sendQueue.emplace(msg);
    }
    if (conn->connState == ConnectionState::CONNECTED) {
        tcpUtil::ConnectionSend(conn);
    }
}

void TCPMgr::SendByRecvLoop(MessageBase *msg, const TCPMgr *tcpmgr, bool remoteLink, bool isExactNotRemote) const
{
    (void)recvEvloop->AddFuncToEvLoop(
        [msg, tcpmgr, remoteLink, isExactNotRemote] { TCPMgr::Send(msg, tcpmgr, remoteLink, isExactNotRemote); });
}

Connection *TCPMgr::FindSendMsgConn(MessageBase *msg, bool remoteLink, bool exactNotRemote)
{
    Connection *conn = LinkMgr::GetLinkMgr()->FindLink(msg->to.Url(), remoteLink, exactNotRemote);
    if (conn == nullptr) {
        BUSLOG_DEBUG("send,not found link and to connect, from:{},to:{},remoteLink:{}", advertiseUrl, msg->to.Url(),
                     remoteLink);
        if (remoteLink && (!exactNotRemote)) {
            BUSLOG_ERROR("send,not found remote link and send fail, name:{},from:{},to:{}", msg->name, advertiseUrl,
                         msg->to.Url());
            auto *ptr = msg;
            delete ptr;
            ptr = nullptr;
            return nullptr;
        }
        this->SendByRecvLoop(msg, this, remoteLink, exactNotRemote);
        return nullptr;
    }
    return conn;
}

int TCPMgr::Send(MessageBase *msg, bool remoteLink, bool isExactNotRemote)
{
    BUSLOG_DEBUG("send msg,remoteLink:{},isExactNotRemote:{},name:{},from:{},to:{}", remoteLink, isExactNotRemote,
                 msg->name, advertiseUrl, msg->to.Url());

    return sendEvloop->AddFuncToEvLoop([msg, this, remoteLink, isExactNotRemote] {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        // search connection by the target address
        bool exactNotRemote = isHttpKmsg || isExactNotRemote;
        Connection *conn = this->FindSendMsgConn(msg, remoteLink, exactNotRemote);
        if (conn == nullptr) {
            return;
        }
        if (conn->connState != CONNECTED && conn->sendQueue.size() >= SENDMSG_QUEUELEN) {
            BUSLOG_WARN("msg dropped, name:{},fd:{},to:{},remote:{}", msg->name, conn->fd, conn->to, conn->isRemote);
            auto *ptr = msg;
            delete ptr;
            ptr = nullptr;

            return;
        }
        if (conn->connState == ConnectionState::CLOSE || conn->connState == ConnectionState::DISCONNECTING) {
            this->SendByRecvLoop(msg, this, remoteLink, exactNotRemote);
            return;
        }

        if (!conn->isRemote && !exactNotRemote && conn->priority == ConnectionPriority::PRI_LOW) {
            Connection *remoteConn = LinkMgr::GetLinkMgr()->ExactFindLink(msg->to.Url(), true);
            if (remoteConn != nullptr && remoteConn->connState == ConnectionState::CONNECTED) {
                conn = remoteConn;
            }
        }

        BUSLOG_DEBUG("send msg,fd:{},name:{},from:{},to:{}", conn->fd, msg->name, advertiseUrl, msg->to.Url());
        outTcpBufSize += msg->body.size();
        if (conn->sendTotalLen == 0) {
            EvbufMgr::PrepareSendMsg(conn, msg, advertiseUrl, isHttpKmsg);
        } else {
            conn->sendQueue.emplace(msg);
        }
        if (conn->connState == ConnectionState::CONNECTED) {
            tcpUtil::ConnectionSend(conn);
        }
    });
}

void TCPMgr::CollectMetrics()
{
    (void)sendEvloop->AddFuncToEvLoop([this] {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        Connection *maxConn = LinkMgr::GetLinkMgr()->FindMaxLink();
        Connection *fastConn = LinkMgr::GetLinkMgr()->FindFastLink();

        if (tcpMsgHandler != nullptr) {
            IntTypeMetrics intMetrics;
            StringTypeMetrics stringMetrics;
            bool needSendMetrics = false;

            if (maxConn != nullptr) {
                intMetrics.push(maxConn->fd);
                intMetrics.push(maxConn->errCode);
                intMetrics.push(maxConn->sendMetrics->sendSum);
                intMetrics.push(maxConn->sendMetrics->sendMaxSize);
                stringMetrics.push(maxConn->to);
                stringMetrics.push(maxConn->sendMetrics->lastSucMsgName);
                stringMetrics.push(maxConn->sendMetrics->lastFailMsgName);
                needSendMetrics = true;
            }
            if (fastConn != nullptr && fastConn->Different(maxConn)) {
                intMetrics.push(fastConn->fd);
                intMetrics.push(fastConn->errCode);
                intMetrics.push(fastConn->sendMetrics->sendSum);
                intMetrics.push(fastConn->sendMetrics->sendMaxSize);
                stringMetrics.push(fastConn->to);
                stringMetrics.push(fastConn->sendMetrics->lastSucMsgName);
                stringMetrics.push(fastConn->sendMetrics->lastFailMsgName);
                needSendMetrics = true;
            }
            if (needSendMetrics) {
                std::unique_ptr<MetricsMessage> metricMessage(new (std::nothrow) MetricsMessage(
                    AID(), SYSMGR_ACTOR_NAME, METRICS_SEND_MSGNAME, intMetrics, stringMetrics));
                BUS_OOM_EXIT(metricMessage);

                std::unique_ptr<MessageLocal> localMsg(new (std::nothrow) MessageLocal(
                    AID(), SYSMGR_ACTOR_NAME, METRICS_SEND_MSGNAME, metricMessage.release()));
                BUS_OOM_EXIT(localMsg);
                // msg owner transferred to msg_handler
                tcpMsgHandler(std::move(localMsg));
            }
        }

        LinkMgr::GetLinkMgr()->RefreshMetrics();
    });
}

int TCPMgr::Send(std::unique_ptr<MessageBase> &&msg, bool remoteLink, bool isExactNotRemote)
{
    return Send(msg.release(), remoteLink, isExactNotRemote);
}

void TCPMgr::Link(const AID &sAid, const AID &dAid)
{
    BUSLOG_DEBUG("link, sAid:{},dAid:{}", std::string(sAid), std::string(dAid));

    (void)recvEvloop->AddFuncToEvLoop([sAid, dAid, this] {
        std::string to = dAid.Url();
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        // search connection by the target address
        Connection *conn = LinkMgr::GetLinkMgr()->FindLink(to, false, isHttpKmsg);
        if (conn == nullptr) {
            BUSLOG_INFO("not found link, sAid:{}, dAid:{}", std::string(sAid), std::string(dAid));
            conn = new (std::nothrow) Connection();
            if (conn == nullptr) {
                BUSLOG_ERROR("new Connection fail and link fail, sAid:{},dAid:{}", std::string(sAid),
                             std::string(dAid));
                SendExitMsg(sAid, dAid);
                return;
            }
            conn->from = advertiseUrl;
            conn->to = to;

            conn->recvEvloop = this->recvEvloop;
            conn->sendEvloop = this->sendEvloop;
            ConnectionUtil::SetSocketOperate(conn);

            int ret = tcpUtil::DoConnect(to, conn, TCPMgr::EventCallBack, TCPMgr::WriteCallBack, TCPMgr::ReadCallBack);
            if (ret < 0) {
                BUSLOG_ERROR("connection fail and link fail, sAid:{},dAid:{}", std::string(sAid), std::string(dAid));
                SendExitMsg(sAid, dAid);
                if (conn->socketOperate != nullptr) {
                    delete conn->socketOperate;
                    conn->socketOperate = nullptr;
                }
                delete conn;
                return;
            }
            LinkMgr::GetLinkMgr()->AddLink(conn);
        }

        LinkMgr::GetLinkMgr()->AddLinker(conn->fd, sAid, dAid, SendExitMsg);

        BUSLOG_INFO("link, fd:{},sAid:{},dAid:{},remote:{}", conn->fd, std::string(sAid), std::string(dAid),
                    conn->isRemote);
    });
}

void TCPMgr::UnLink(const AID &dAid)
{
    (void)recvEvloop->AddFuncToEvLoop([dAid] {
        std::string to = dAid.Url();
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        if (isHttpKmsg) {
            // When application has set 'LITEBUS_HTTPKMSG_ENABLED',it means sending-link is in links map
            // while accepting-link is differently in remoteLinks map. So we only need to delete link in exact links.
            LinkMgr::GetLinkMgr()->ExactDeleteLink(to, false);
        } else {
            // When application hasn't set 'LITEBUS_HTTPKMSG_ENABLED',it means sending-link and accepting-link is
            // shared
            // So we need to delete link in both links map and remote-links map.
            LinkMgr::GetLinkMgr()->ExactDeleteLink(to, false);
            LinkMgr::GetLinkMgr()->ExactDeleteLink(to, true);
        }
    });
}

void TCPMgr::DoReConnectConn(Connection *conn, const std::string &to, const AID &sAid, const AID &dAid,
                             int &oldFd) const
{
    if (!isHttpKmsg && !conn->isRemote) {
        Connection *remoteConn = LinkMgr::GetLinkMgr()->ExactFindLink(to, true);
        // We will close remote link in rare cases where sending-link and accepting link coexists
        // simultaneously.
        if (remoteConn != nullptr) {
            BUSLOG_INFO("reconnect, close remote connect,fd:{},sAid:{},dAid:{},remote:{},connState:{}", remoteConn->fd,
                        std::string(sAid), std::string(dAid), remoteConn->isRemote,
                        static_cast<std::underlying_type_t<ConnectionState>>(remoteConn->connState));
            LinkMgr::GetLinkMgr()->CloseConnection(remoteConn);
        }
    }

    BUSLOG_INFO("reconnect, close old connect,fd:{},sAid:{},dAid:{},remote:{},connState:{}", conn->fd,
                std::string(sAid), std::string(dAid), conn->isRemote,
                static_cast<std::underlying_type_t<ConnectionState>>(conn->connState));

    oldFd = conn->fd;

    (void)conn->recvEvloop->DelFdEvent(conn->fd);
    conn->socketOperate->Close(conn);

    conn->fd = -1;
    conn->recvLen = 0;

    conn->recvTotalLen = 0;
    conn->recvMsgType = UNKNOWN;
    conn->connState = INIT;
    if (conn->sendTotalLen != 0 && conn->sendMsgBase != nullptr) {
        delete conn->sendMsgBase;
    }
    conn->sendMsgBase = nullptr;
    conn->sendTotalLen = 0;

    if (conn->recvTotalLen != 0 && conn->recvMsgBase != nullptr) {
        delete conn->recvMsgBase;
    }
    conn->recvMsgBase = nullptr;
    conn->recvTotalLen = 0;

    conn->recvState = State::MSG_HEADER;
}

Connection *TCPMgr::CreateRemoteConn(int acceptFd) const
{
    Connection *conn = new (std::nothrow) Connection();
    if (conn == nullptr) {
        (void)close(acceptFd);
        acceptFd = -1;
        return nullptr;
    }

    // init metrics
    conn->sendMetrics = new (std::nothrow) SendMetrics();
    if (conn->sendMetrics == nullptr) {
        (void)close(acceptFd);
        acceptFd = -1;
        delete conn;
        return nullptr;
    }

    conn->fd = acceptFd;
    conn->from = TCPMgr::GetAdvertiseUrl();
    conn->peer = SocketOperate::GetFdPeer(acceptFd);

    conn->isRemote = true;
    conn->recvEvloop = this->recvEvloop;
    conn->sendEvloop = this->sendEvloop;

    conn->eventCallBack = TCPMgr::EventCallBack;
    conn->writeCallBack = TCPMgr::WriteCallBack;
    conn->readCallBack = TCPMgr::ReadCallBack;
    return conn;
}

Connection *TCPMgr::CreateDefaultConn(std::string to) const
{
    Connection *conn = new (std::nothrow) Connection();
    if (conn == nullptr) {
        BUSLOG_ERROR("new Connection fail and reconnect fail, to:{}", to);
        return conn;
    }
    conn->from = advertiseUrl;
    conn->to = to;
    conn->recvEvloop = this->recvEvloop;
    conn->sendEvloop = this->sendEvloop;
    ConnectionUtil::SetSocketOperate(conn);
    return conn;
}

Connection *TCPMgr::CreateSendMsgConn(MessageBase *msg) const
{
    Connection *conn = new (std::nothrow) Connection();
    if (conn == nullptr) {
        BUSLOG_ERROR("send,new Connection fail and send fail, name:{},from:{},to:{}", msg->name, advertiseUrl,
                     msg->to.Url());
        delete msg;
        return nullptr;
    }
    conn->from = advertiseUrl;
    conn->to = msg->to.Url();
    conn->recvEvloop = this->recvEvloop;
    conn->sendEvloop = this->sendEvloop;
    ConnectionUtil::SetSocketOperate(conn);

    int ret =
        tcpUtil::DoConnect(msg->to.Url(), conn, TCPMgr::EventCallBack, TCPMgr::WriteCallBack, TCPMgr::ReadCallBack);
    if (ret < 0) {
        BUSLOG_ERROR("send,connection fail and send fail, name:{},from:{},to:{}", msg->name, advertiseUrl,
                     msg->to.Url());
        if (conn->socketOperate != nullptr) {
            delete conn->socketOperate;
            conn->socketOperate = nullptr;
        }
        delete conn;
        delete msg;
        return nullptr;
    }
    return conn;
}

void TCPMgr::Reconnect(const AID &sAid, const AID &dAid)
{
    (void)sendEvloop->AddFuncToEvLoop([sAid, dAid, this] {
        std::string to = dAid.Url();
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        Connection *conn = LinkMgr::GetLinkMgr()->FindLink(to, false, isHttpKmsg);
        if (conn != nullptr) {
            conn->connState = ConnectionState::CLOSE;
        }

        (void)recvEvloop->AddFuncToEvLoop([sAid, dAid, this] {
            std::string to = dAid.Url();
            int oldFd = -1;
            std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
            Connection *conn = LinkMgr::GetLinkMgr()->FindLink(to, false, isHttpKmsg);
            if (conn != nullptr) {
                // connection already exist
                DoReConnectConn(conn, to, sAid, dAid, oldFd);
            } else {
                // create default connection
                conn = CreateDefaultConn(to);
                if (conn == nullptr) {
                    return;
                }
            }
            int ret = tcpUtil::DoConnect(to, conn, TCPMgr::EventCallBack, TCPMgr::WriteCallBack, TCPMgr::ReadCallBack);
            if (ret < 0) {
                if (conn->socketOperate != nullptr) {
                    delete conn->socketOperate;
                    conn->socketOperate = nullptr;
                }
                if (oldFd != -1) {
                    conn->fd = oldFd;
                }
                BUSLOG_ERROR("connect fail and reconnect fail, sAid:{},dAid:{}", std::string(sAid), std::string(dAid));
                LinkMgr::GetLinkMgr()->CloseConnection(conn);
                return;
            }
            if (oldFd != -1) {
                if (LinkMgr::GetLinkMgr()->SwapLinkerSocket(oldFd, conn->fd)) {
                }
                // else not found
            } else {
                LinkMgr::GetLinkMgr()->AddLink(conn);
            }
            LinkMgr::GetLinkMgr()->AddLinker(conn->fd, sAid, dAid, SendExitMsg);
            BUSLOG_INFO("reconnect,fd:{},sAid:{},dAid:{}", conn->fd, std::string(sAid), std::string(dAid));
        });
    });
}

void TCPMgr::FinishDestruct()
{
    if (sendEvloop != nullptr) {
        BUSLOG_INFO("delete send event loop");
        sendEvloop->Finish();
        delete sendEvloop;
        sendEvloop = nullptr;
    }

    if (recvEvloop != nullptr) {
        BUSLOG_INFO("delete recv event loop");
        recvEvloop->Finish();
        if (recvEvloop->DelFdEvent(serverFd) != BUS_OK) {
            BUSLOG_ERROR("failed to delete server fd event");
        }
        delete recvEvloop;
        recvEvloop = nullptr;
    }

    if (serverFd > 0) {
        (void)close(serverFd);
        serverFd = -1;
    }
}

void TCPMgr::Finish()
{
    FinishDestruct();
}

uint64_t TCPMgr::GetOutBufSize()
{
    return outTcpBufSize;
}

uint64_t TCPMgr::GetTCPOutSize()
{
    return outTcpBufSize;
}

void TCPMgr::SetTCPOutSize(uint64_t size)
{
    outTcpBufSize = size;
}

uint64_t TCPMgr::GetInBufSize()
{
    return 1;
}

std::string TCPMgr::GetAdvertiseUrl()
{
    return advertiseUrl;
}

bool TCPMgr::IsHttpKmsg()
{
    return isHttpKmsg;
}

#ifdef HTTP_ENABLED
void TCPMgr::RegisterRecvHttpCallBack(RecvCallBack reqCb, RecvCallBack rspCb, CheckConCallBack conCheckCb)
{
    httpReqCb = reqCb;
    httpRspCb = rspCb;
    httpConCheckCb = conCheckCb;
}

// for http reply
int TCPMgr::Send(MessageBase *msg, Connection *connection, int conSeq)
{
    return recvEvloop->AddFuncToEvLoop([msg, connection, conSeq]() mutable {
        std::lock_guard<std::mutex> lock(LinkMgr::linkMutex);
        if (httpConCheckCb(conSeq)) {
            connection->sendQueue.emplace(msg);
            tcpUtil::ConnectionSend(connection);
            BUSLOG_DEBUG("send reply successfully,fd={}, conSeq={}", connection->fd, connection->sequence);
        } else {
            delete msg;
            msg = nullptr;
        }
    });
}
#endif

void TCPMgr::LinkRecycleCheck(int recyclePeroid)
{
    (void)recvEvloop->AddFuncToEvLoop([recyclePeroid] {
        Connection *conn = nullptr;
        int idleConnCount = 0;
        auto iter = LinkMgr::GetLinkMgr()->httpRemoteLinks.begin();
        while (iter != LinkMgr::GetLinkMgr()->httpRemoteLinks.end()) {
            conn = iter->second;
            conn->noCommTime++;
            if ((conn->noCommTime > recyclePeroid) && (idleConnCount <= MAX_RECYCLE_LINK_COUNT)) {
                BUSLOG_WARN("timeout conn, fd:{},to:{},peer:{}", conn->fd, conn->to, conn->peer);
                iter = LinkMgr::GetLinkMgr()->httpRemoteLinks.erase(iter);
                conn->timeoutRemoved = true;
                conn->connState = ConnectionState::DISCONNECTING;
                if (conn->eventCallBack != nullptr) {
                    conn->eventCallBack(conn);
                }
                idleConnCount++;
            } else {
                ++iter;
            }
        }
    });
}

}    // namespace litebus
