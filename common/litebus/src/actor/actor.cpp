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

#include "actor/actor.hpp"

#include "actor/actormgr.hpp"
#include "actor/actorpolicyinterface.hpp"
#include "actor/buslog.hpp"
#include "actor/iomgr.hpp"
#include "utils/os_utils.hpp"
#include "utils/time_util.hpp"

namespace litebus {
const int32_t SIGNATURE_LENGTH = 3;

const int32_t ACCESS_KEY_INDEX = 1;
const int32_t ACCESS_KEY_SPLIT_LENGTH = 2;
const int32_t ACCESS_KEY_SPLIT_VALUE_INDEX = 1;

const int32_t TIMESTAMP_INDEX = 0;
const int32_t TIMESTAMP_SPLIT_LENGTH = 2;
const int32_t TIMESTAMP_SPLIT_VALUE_INDEX = 1;

ActorBase::ActorBase(const std::string &name)
    : actorThread(nullptr), id(name, ActorMgr::GetActorMgrRef()->GetUrl()), actionFunctions()
{
    auto enableAKSK = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enableAKSK.IsSome() && enableAKSK.Get() == "1") {
        auto tmpAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
        if (tmpAk.IsSome()) {
            id.SetAk(tmpAk.Get());
            accessKey_ = std::make_shared<std::string>(tmpAk.Get());
        } else {
            BUSLOG_ERROR("Failed to obtain the access key when 2fa enabled");
        }

        auto secretKey = (::getenv(litebus::os::LITEBUS_SECRET_KEY.c_str()));
        if (secretKey != nullptr) {
            secretKey_ = std::make_shared<SensitiveValue>(secretKey);
        } else {
            BUSLOG_ERROR("Failed to obtain the secret key when 2fa enabled");
        }
    }
}

ActorBase::~ActorBase()
{
}

void ActorBase::Spawn(std::shared_ptr<ActorBase> &, std::unique_ptr<ActorPolicy> thread)
{
    // lock here or await(). and unlock at Quit() or at aweit.
    waiterLock.lock();

    actorThread = std::move(thread);
}
void ActorBase::SetRunningStatus(bool start)
{
    actorThread->SetRunningStatus(start);
}

void ActorBase::Await()
{
    std::string actorName = id.Name();
    // lock here or at spawn(). and unlock here or at worker(). wait for the worker to finish.
    BUSLOG_DEBUG("ACTOR is waiting for terminate to finish. a={}", actorName);
    waiterLock.lock();
    waiterLock.unlock();
    BUSLOG_DEBUG("ACTOR succeeded in waiting. a={}", actorName);
}
void ActorBase::Terminate()
{
    std::unique_ptr<MessageBase> msg(new (std::nothrow) MessageBase("Terminate", MessageBase::Type::KTERMINATE));
    BUS_OOM_EXIT(msg)
    (void)EnqueMessage(std::move(msg));
}

void ActorBase::HandlekMsg(std::unique_ptr<MessageBase> &msg)
{
    if (!SignatureVerification(msg)) {    // can be implemented by user
        return;
    }
    auto it = actionFunctions.find(msg->Name());
    if (it != actionFunctions.end()) {
        ActorFunction &func = it->second;
        func(msg);
    } else {
        BUSLOG_WARN("ACTOR can not find function for message, a={},m={}", id.Name(), msg->Name());
    }
}

int ActorBase::EnqueMessage(std::unique_ptr<MessageBase> msg)
{
    BUSLOG_DEBUG("enqueue message, actor={},msg={}", id.Name(), msg->Name());
    return actorThread->EnqueMessage(msg);
}

void ActorBase::Quit()
{
    Finalize();
    actorThread->Terminate(this);
    // lock at spawn(), unlock here.
    waiterLock.unlock();
}

void ActorBase::Run()
{
    for (;;) {
        auto msgs = actorThread->GetMsgs();
        if (msgs == nullptr) {
            return;
        }
        for (auto it = msgs->begin(); it != msgs->end(); ++it) {
            std::unique_ptr<MessageBase> &msg = *it;
            if (msg == nullptr) {
                continue;
            }
            BUSLOG_DEBUG("dequeue message, actor={},msg={}", id.Name(), msg->Name());
            AddMsgRecord(msg->Name());
            switch (msg->GetType()) {
                case MessageBase::Type::KMSG:
                case MessageBase::Type::KUDP: {
                    if (Filter(msg)) {
                        continue;
                    }
                    this->HandlekMsg(msg);
                    break;
                }
                case MessageBase::Type::KHTTP: {
                    this->HandleHttp(std::move(msg));
                    break;
                }
                case MessageBase::Type::KASYNC: {
                    msg->Run(this);
                    break;
                }
                case MessageBase::Type::KLOCAL: {
                    this->HandleLocalMsg(std::move(msg));
                    break;
                }
                case MessageBase::Type::KTERMINATE: {
                    this->Quit();
                    return;
                }
                case MessageBase::Type::KEXIT: {
                    this->Exited(msg->From());
                    break;
                }
            }
        }
        msgs->clear();
    }
}

int ActorBase::Send(const AID &to, std::unique_ptr<MessageBase> msg)
{
    msg->SetFrom(id);
    Signature(msg);  // can be implemented by user
    return ActorMgr::GetActorMgrRef()->Send(to, std::move(msg));
}
int ActorBase::Send(const AID &to, std::string &&name, std::string &&strMsg, bool remoteLink, bool isExactNotRemote)
{
    std::unique_ptr<MessageBase> msg(
        new (std::nothrow) MessageBase(this->id, to, std::move(name), std::move(strMsg), MessageBase::Type::KMSG));
    BUS_OOM_EXIT(msg)
    Signature(msg);  // can be implemented by user
    return ActorMgr::GetActorMgrRef()->Send(to, std::move(msg), remoteLink, isExactNotRemote);
}

// register the message handle
void ActorBase::Receive(const std::string &msgName, ActorFunction &&func)
{
    if (actionFunctions.find(msgName) != actionFunctions.end()) {
        BUSLOG_ERROR("ACTOR function's name conflicts, a={},f={}", id.Name(), msgName);
        BUS_EXIT("function's name conflicts");
        return;
    }
    (void)actionFunctions.emplace(msgName, std::move(func));
    return;
}

int ActorBase::Link(const AID &to) const
{
    auto io = ActorMgr::GetIOMgrRef(to);
    if (io != nullptr) {
        if (to.OK()) {
            io->Link(this->GetAID(), to);
            return ERRORCODE_SUCCESS;
        } else {
            return ACTOR_PARAMER_ERR;
        }
    } else {
        return IO_NOT_FIND;
    }
}
int ActorBase::UnLink(const AID &to) const
{
    auto io = ActorMgr::GetIOMgrRef(to);
    if (io != nullptr) {
        if (to.OK()) {
            io->UnLink(to);
            return ERRORCODE_SUCCESS;
        } else {
            return ACTOR_PARAMER_ERR;
        }
    } else {
        return IO_NOT_FIND;
    }
}

int ActorBase::Reconnect(const AID &to) const
{
    auto io = ActorMgr::GetIOMgrRef(to);
    if (io != nullptr) {
        if (to.OK()) {
            io->Reconnect(this->GetAID(), to);
            return ERRORCODE_SUCCESS;
        } else {
            return ACTOR_PARAMER_ERR;
        }
    } else {
        return IO_NOT_FIND;
    }
}

uint64_t ActorBase::GetOutBufSize(const AID &to) const
{
    auto io = ActorMgr::GetIOMgrRef(to);
    if (io != nullptr) {
        return io->GetOutBufSize();
    } else {
        return 0;
    }
}

uint64_t ActorBase::GetInBufSize(const AID &to) const
{
    auto io = ActorMgr::GetIOMgrRef(to);
    if (io != nullptr) {
        return io->GetInBufSize();
    } else {
        return 0;
    }
}

int ActorBase::AddRuleUdp(const std::string &peer, int recordNum) const
{
    const std::string udp = BUS_UDP;
    auto io = ActorMgr::GetIOMgrRef(udp);
    if (io != nullptr) {
        return io->AddRuleUdp(peer, recordNum);
    } else {
        return 0;
    }
}

void ActorBase::DelRuleUdp(const std::string &peer, bool outputLog) const
{
    const std::string udp = BUS_UDP;
    auto io = ActorMgr::GetIOMgrRef(udp);
    if (io != nullptr) {
        io->DelRuleUdp(peer, outputLog);
    }
}

std::string ActorBase::GenSignature(const std::unique_ptr<MessageBase> &msg, const std::string &timestamp) const
{
    std::stringstream rss;  // example: ip:port/actor/function\n[1234567890abcdef]{64}
    rss << msg->to.UnfixUrl() << "/" << msg->to.Name() << "/" << msg->name << "\n";
    hmac::SHA256AndHex(msg->body, rss);  // append body which is sha256 and hex

    // example:
    // 20250103T075746Z
    // [1234567890abcdef]{64}
    std::stringstream ss;
    ss << timestamp << "\n";
    hmac::SHA256AndHex(rss.str(), ss);  // sha256 and hex for path and body

    return "HMAC-SHA256 timestamp=" + timestamp + ",access_key=" + *msg->accessKey +
           ",signature=" + hmac::HMACAndSHA256(*msg->secretKey, ss.str());
}

void ActorBase::Signature(const std::unique_ptr<MessageBase> &msg)
{
    if (msg->from.GetIp() == msg->to.GetIp() || msg->to.GetIp() == "127.0.0.1") {
        // if on the same node/pod, not signature
        return;
    }

    // should config the different key for message to the different aid
    if (msg->accessKey == nullptr || msg->secretKey == nullptr) {
        if (accessKey_ == nullptr || secretKey_ == nullptr) {
            // if key not exist, not signature
            return;
        }

        // use default access and secret
        msg->accessKey = accessKey_;
        msg->secretKey = secretKey_;
    }

    std::string timestamp = time::GetCurrentUTCTime();
    msg->signature = std::move(GenSignature(msg, timestamp));
}

bool ActorBase::SignatureVerification(const std::unique_ptr<MessageBase> &msg)
{
    if (msg->type == MessageBase::Type::KUDP && msg->from.GetIp() == "127.0.0.1") {
        // Allow directly for heartbeat
        return true;
    }

    // maybe ip:port or ip
    std::string from = msg->peer;
    if (const size_t pos = msg->peer.find(':'); pos != std::string::npos) {
        from = msg->peer.substr(0, pos); // ip:port
    }
    if (from == msg->to.GetIp() || from == "127.0.0.1") {
        // Allow directly in the pod or process.
        return true;
    }
    // Cannot obtain the peer IP. The signature must be verified.

    // should config the different key for message from the different aid
    if (msg->accessKey == nullptr || msg->secretKey == nullptr) {
        if (accessKey_ == nullptr || secretKey_ == nullptr) {
            // if key not exist, not signature
            return true;
        }

        // use default access and secret
        msg->accessKey = accessKey_;
        msg->secretKey = secretKey_;
    }

    // secret key not empty, but signature empty
    if (msg->signature.empty()) {
        BUSLOG_ERROR("Illegal Signature: empty, {} from {} to {}", msg->name, msg->from.operator std::string(),
                     msg->to.operator std::string());
        return false;
    }

    auto splits = litebus::strings::Split(msg->signature, ",");
    if (splits.size() != SIGNATURE_LENGTH) {
        BUSLOG_ERROR("Illegal Signature: illegal size, {} from {} to {}", msg->name, msg->from.operator std::string(),
                     msg->to.operator std::string());
        return false;
    }

    auto timestamp = litebus::strings::Split(splits[TIMESTAMP_INDEX], "=");
    if (timestamp.size() != TIMESTAMP_SPLIT_LENGTH) {
        BUSLOG_ERROR("Illegal Signature: illegal timestamp, {} from {} to {}", msg->name,
                     msg->from.operator std::string(), msg->to.operator std::string());
        return false;
    }

    auto accessKey = litebus::strings::Split(splits[ACCESS_KEY_INDEX], "=");
    if (accessKey.size() != ACCESS_KEY_SPLIT_LENGTH) {
        BUSLOG_ERROR("Illegal Signature: illegal ak, {} from {} to {}", msg->name, msg->from.operator std::string(),
                     msg->to.operator std::string());
        return false;
    }

    if (accessKey[ACCESS_KEY_SPLIT_VALUE_INDEX] != *msg->accessKey) {
        BUSLOG_ERROR("Illegal Signature: unknown ak, {} from {} to {}", msg->name, msg->from.operator std::string(),
                     msg->to.operator std::string());
        return false;
    }

    std::string signature = GenSignature(msg, timestamp[TIMESTAMP_SPLIT_VALUE_INDEX]);
    return signature == msg->signature;
}
}  // namespace litebus
