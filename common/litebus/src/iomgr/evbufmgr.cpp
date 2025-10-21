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

#include <memory>
#include <string>

#include <securec.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "utils/os_utils.hpp"
#include "iomgr/evbufmgr.hpp"

namespace litebus {

std::string g_advertiseAddr;

void EvbufMgr::HeaderNtoH(MsgHeader *header)
{
    header->nameLen = ntohl(header->nameLen);
    header->toLen = ntohl(header->toLen);
    header->fromLen = ntohl(header->fromLen);
    header->signatureLen = ntohl(header->signatureLen);
    header->bodyLen = ntohl(header->bodyLen);
}

// write message to the buffer
void EvbufMgr::PrepareRecvMsg(Connection *conn)
{
    size_t recvNameLen = static_cast<size_t>(conn->recvHeader.nameLen);
    size_t recvToLen = static_cast<size_t>(conn->recvHeader.toLen);
    size_t recvFromLen = static_cast<size_t>(conn->recvHeader.fromLen);
    size_t signatureLen = static_cast<size_t>(conn->recvHeader.signatureLen);
    size_t recvBodyLen = static_cast<size_t>(conn->recvHeader.bodyLen);
    if (recvNameLen > MAX_KMSG_NAME_LEN || recvToLen > MAX_KMSG_TO_LEN || recvFromLen > MAX_KMSG_FROM_LEN ||
        recvBodyLen > MAX_KMSG_BODY_LEN || signatureLen > MAX_KMSG_SIGNATURE_LEN) {
        BUSLOG_ERROR("Drop invalid tcp data.");
        conn->connState = ConnectionState::DISCONNECTING;
        return;
    }

    unsigned int i = 0;
    MessageBase *msg = new (std::nothrow) MessageBase();
    BUS_OOM_EXIT(msg);

    msg->name.resize(recvNameLen);
    conn->recvTo.resize(recvToLen);
    conn->recvFrom.resize(recvFromLen);
    msg->signature.resize(signatureLen);
    msg->body.resize(recvBodyLen);

    conn->recvIov[i].iov_base = const_cast<char *>(msg->name.data());
    conn->recvIov[i].iov_len = msg->name.size();
    ++i;
    conn->recvIov[i].iov_base = const_cast<char *>(conn->recvTo.data());
    conn->recvIov[i].iov_len = conn->recvTo.size();
    ++i;
    conn->recvIov[i].iov_base = const_cast<char *>(conn->recvFrom.data());
    conn->recvIov[i].iov_len = conn->recvFrom.size();
    ++i;
    conn->recvIov[i].iov_base = const_cast<char *>(msg->signature.data());
    conn->recvIov[i].iov_len = msg->signature.size();
    ++i;
    conn->recvIov[i].iov_base = const_cast<char *>(msg->body.data());
    conn->recvIov[i].iov_len = msg->body.size();
    ++i;

    conn->recvMsg.msg_iov = conn->recvIov;
    conn->recvMsg.msg_iovlen = i;
    conn->recvTotalLen =
        msg->name.size() + conn->recvTo.size() + conn->recvFrom.size() + msg->signature.size() + msg->body.size();
    conn->recvMsgBase = msg;
}

void SetAdvertiseAddr(const std::string &advertiseUrl)
{
    size_t index = advertiseUrl.find(URL_PROTOCOL_IP_SEPARATOR);
    if (index == std::string::npos) {
        g_advertiseAddr = advertiseUrl;
    } else {
        g_advertiseAddr = advertiseUrl.substr(index + URL_PROTOCOL_IP_SEPARATOR.length());
    }
}

std::string EncodeHttpMsg(MessageBase *msg)
{
    static const std::string postLineBegin = std::string() + "POST /";
    static const std::string postLineEnd = std::string() + " HTTP/1.1\r\n";

    static const std::string userAgentLineBegin = std::string() + "User-Agent: libprocess/";

    static const std::string fromLineBegin = std::string() + "Libprocess-From: ";
    static const std::string AUTHORIZATION_LINE_BEGIN = std::string() + "Authorization: ";

    static const std::string connectLine = std::string() + "Connection: Keep-Alive\r\n";

    static const std::string hostLine = std::string() + "Host: \r\n";

    static const std::string chunkedBeginLine = std::string() + "Transfer-Encoding: chunked\r\n\r\n";
    static const std::string chunkedEndLine = std::string() + "\r\n" + "0\r\n" + "\r\n";
    static const std::string commonEndLine = std::string() + "\r\n";

    std::string postLine;
    if (msg->To().Name() != "") {
        postLine = postLineBegin + msg->To().Name() + "/" + msg->Name() + postLineEnd;
    } else {
        postLine = postLineBegin + msg->Name() + postLineEnd;
    }

    std::string userAgentLine = userAgentLineBegin + msg->From().Name() + "@" + g_advertiseAddr + commonEndLine;

    std::string fromLine = fromLineBegin + msg->From().Name() + "@" + g_advertiseAddr + commonEndLine;

    std::string authorizationLine;
    if (!msg->signature.empty()) {  // when not empty
        authorizationLine = AUTHORIZATION_LINE_BEGIN + msg->signature + commonEndLine;
    }

    if (msg->Body().size() > 0) {
        std::ostringstream bodyLine;
        bodyLine << std::hex << msg->Body().size() << "\r\n";
        (void)bodyLine.write(msg->Body().data(), static_cast<std::streamsize>(msg->Body().size()));
        return postLine + userAgentLine + fromLine + connectLine + hostLine + authorizationLine + chunkedBeginLine +
               bodyLine.str() + chunkedEndLine;
    }

    return postLine + userAgentLine + fromLine + connectLine + hostLine + authorizationLine + commonEndLine;
}

// write message to the buffer
void EvbufMgr::PrepareSendMsg(Connection *conn, MessageBase *msg, const std::string &advertiseUrl, bool isHttpKmsg)
{
    unsigned int i = 0;
    if (msg->type == MessageBase::Type::KMSG) {
        if (!isHttpKmsg) {
            conn->sendTo = msg->to;
            conn->sendFrom = msg->from.Name() + "@" + advertiseUrl;
            if (msg->name.size() > MAX_KMSG_NAME_LEN || conn->sendTo.size() > MAX_KMSG_TO_LEN
                || conn->sendFrom.size() > MAX_KMSG_FROM_LEN || msg->body.size() > MAX_KMSG_BODY_LEN
                || msg->signature.size() > MAX_KMSG_SIGNATURE_LEN) {
                BUSLOG_ERROR("Drop invalid send tcp data.");
                return;
            }
            conn->sendHeader.nameLen = htonl(static_cast<uint32_t>(msg->name.size()));
            conn->sendHeader.toLen = htonl(static_cast<uint32_t>(conn->sendTo.size()));
            conn->sendHeader.fromLen = htonl(static_cast<uint32_t>(conn->sendFrom.size()));
            conn->sendHeader.signatureLen = htonl(static_cast<uint32_t>(msg->signature.size()));
            conn->sendHeader.bodyLen = htonl(static_cast<uint32_t>(msg->body.size()));

            conn->sendIov[i].iov_base = &conn->sendHeader;
            conn->sendIov[i++].iov_len = sizeof(conn->sendHeader);

            conn->sendIov[i].iov_base = const_cast<char *>(msg->name.data());
            conn->sendIov[i++].iov_len = msg->name.size();

            conn->sendIov[i].iov_base = const_cast<char *>(conn->sendTo.data());
            conn->sendIov[i++].iov_len = conn->sendTo.size();

            conn->sendIov[i].iov_base = const_cast<char *>(conn->sendFrom.data());
            conn->sendIov[i++].iov_len = conn->sendFrom.size();

            conn->sendIov[i].iov_base = const_cast<char *>(msg->signature.data());
            conn->sendIov[i++].iov_len = msg->signature.size();

            conn->sendIov[i].iov_base = const_cast<char *>(msg->body.data());
            conn->sendIov[i++].iov_len = msg->body.size();

            conn->sendMsg.msg_iov = conn->sendIov;
            conn->sendMsg.msg_iovlen = i;
            conn->sendTotalLen = sizeof(conn->sendHeader) + msg->name.size() + conn->sendTo.size()
                                 + conn->sendFrom.size() + msg->signature.size() + msg->body.size();
            conn->sendMsgBase = msg;

            // update metrics
            conn->sendMetrics->UpdateMax(msg->signature.size() + msg->body.size());
            conn->sendMetrics->UpdateName(msg->name);

            return;
        } else {
            if (g_advertiseAddr.empty()) {
                SetAdvertiseAddr(advertiseUrl);
            }

            msg->body = EncodeHttpMsg(msg);
        }
    }

    conn->sendIov[i].iov_base = const_cast<char *>(msg->body.data());
    conn->sendIov[i++].iov_len = msg->body.size();

    conn->sendMsg.msg_iov = conn->sendIov;
    conn->sendMsg.msg_iovlen = i;
    conn->sendTotalLen = msg->body.size();
    conn->sendMsgBase = msg;

    // update metrics
    conn->sendMetrics->UpdateMax(msg->body.size());
    conn->sendMetrics->UpdateName(msg->name);
}

}    // namespace litebus
