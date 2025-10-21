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

#ifndef __LITEBUS_TCPMGR_H__
#define __LITEBUS_TCPMGR_H__

#include <string>

#include "actor/iomgr.hpp"
#include "evloop/evloop.hpp"
#include "iomgr/evbufmgr.hpp"
#include "iomgr/linkmgr.hpp"
#include "iomgr/socket_operate.hpp"

namespace litebus {

using RecvCallBack = int (*)(Connection *connection, IOMgr::MsgHandler msgHandler);
#ifdef HTTP_ENABLED
using CheckConCallBack = bool (*)(int conSeq);
#endif

namespace tcpUtil {
int DoConnect(const std::string &to, Connection *conn, ConnectionCallBack eventCallBack,
              ConnectionCallBack writeCallBack, ConnectionCallBack readCallBack);
void ConnEstablishedEvHandler(int fd, uint32_t events, void *context);
void CleanUp(int fd, Connection *conn, uint32_t error, int soError);
void OnAccept(int server, uint32_t events, void *arg);
void ConnectionSend(Connection *conn);

}    // namespace tcpUtil

class TCPMgr : public IOMgr {
public:
    TCPMgr(){};
    TCPMgr(const TCPMgr &) = delete;
    TCPMgr &operator=(const TCPMgr &) = delete;
    ~TCPMgr() override;
    int Send(std::unique_ptr<MessageBase> &&msg, bool remoteLink = false, bool isExactNotRemote = false);
    int Send(MessageBase *msg, bool remoteLink = false, bool isExactNotRemote = false);
    void Link(const AID &sAid, const AID &dAid) override;
    Connection *FindSendMsgConn(MessageBase *msg, bool remoteLink, bool exactNotRemote);
    Connection *CreateDefaultConn(std::string to) const;
    Connection *CreateRemoteConn(int acceptFd) const;
    Connection *CreateSendMsgConn(MessageBase *msg) const;
    void DoReConnectConn(Connection *conn, const std::string &to, const AID &sAid, const AID &dAid, int &oldFd) const;
    void Reconnect(const AID &sAid, const AID &dAid) override;
    void UnLink(const AID &dAid) override;
    void RegisterMsgHandle(IOMgr::MsgHandler handler) override;
    bool Init() override;
    void Finish() override;
    void FinishDestruct();
    bool StartIOServer(const std::string &url, const std::string &aAdvertiseUrl) override;
    uint64_t GetOutBufSize() override;
    void CollectMetrics() override;
    static uint64_t GetTCPOutSize();
    static void SetTCPOutSize(uint64_t size);
    uint64_t GetInBufSize() override;
    int AddRuleUdp(std::string, int) override
    {
        return 1;
    }

    void DelRuleUdp(std::string, bool) override{};
    void LinkRecycleCheck(int recyclePeroid) override;

    // connect result handle
    static int AddConnEstablishedHandler(Connection *conn);
    static bool ConnEstablishedSSL(Connection *conn, int fd);

    static void ReadCallBack(void *context);
    static void EventCallBack(void *context);
    static void WriteCallBack(void *context);

    // connect
    static int TcpConnect(Connection *conn, const struct sockaddr *sa, socklen_t saLen);

    // send msg
    void SendByRecvLoop(MessageBase *msg, const TCPMgr *tcpmgr, bool remoteLink, bool isExactNotRemote) const;
    static void Send(MessageBase *msg, const TCPMgr *tcpmgr, bool remoteLink, bool isExactNotRemote);
    static void SendExitMsg(const std::string &from, const std::string &to);
    static int RecvMsg(Connection *conn);

    static std::string GetAdvertiseUrl();
    static bool IsHttpKmsg();
    static void InitRemoteLinkMaxSetting();

#ifdef HTTP_ENABLED
    int Send(MessageBase *msg, Connection *connection, int conSeq);
    static void RegisterRecvHttpCallBack(RecvCallBack reqCb, RecvCallBack rspCb, CheckConCallBack conCheckCb);
#endif

private:
    // url=tcp@ip:port
    std::string url_;

    int serverFd = -1;
    static uint64_t outTcpBufSize;
    static IOMgr::MsgHandler tcpMsgHandler;
#ifdef HTTP_ENABLED
    static RecvCallBack httpReqCb;
    static RecvCallBack httpRspCb;
    static CheckConCallBack httpConCheckCb;
#endif
    static std::string advertiseUrl;
    static bool isHttpKmsg;
    EvLoop *recvEvloop = nullptr;
    EvLoop *sendEvloop = nullptr;
    static int maxRemoteLinkCount;
    friend void tcpUtil::OnAccept(int server, uint32_t events, void *arg);
};

};    // namespace litebus

#endif
