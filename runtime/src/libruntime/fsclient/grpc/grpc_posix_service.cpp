/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#include "grpc_posix_service.h"
#include "src/libruntime/utils/utils.h"

#include "fs_intf_grpc_server_reader_writer.h"

namespace YR {
namespace Libruntime {
const std::string FUNCTION_PROXY = "function-proxy";

bool GrpcPosixService::CompareInstanceID(grpc::ServerContext *context) const
{
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto iter = metadata.find("instance_id");
    if (iter == metadata.end()) {
        YRLOG_WARN("Can not found INSTANCE_ID in metadata.");
        return true;
    }

    auto instanceId = std::string(iter->second.data(), iter->second.size());
    if (iter->second.starts_with("driver")) {
        YRLOG_DEBUG("driver mode: {}.", instanceId);
        return true;
    }

    if (Config::Instance().INSTANCE_ID() == instanceId) {
        return true;
    }

    YRLOG_WARN("instance id not match, except: {} get: {}.", Config::Instance().INSTANCE_ID(), instanceId);
    return false;
}

ErrorInfo GrpcPosixService::Start()
{
    grpc::EnableDefaultHealthCheckService(true);

    grpc::ServerBuilder builder;
    if (!Config::Instance().POSIX_LISTEN_ADDR().empty()) {
        std::string ip = "";
        int32_t port = 0;
        ParseIpAddr(Config::Instance().POSIX_LISTEN_ADDR(), ip, port);
        listeningIpAddr = ip;
    }
    uint32_t maxGrpcSize = Config::Instance().MAX_GRPC_SIZE() * SIZE_MEGA_BYTES;
    builder.AddListeningPort(listeningIpAddr + ":" + std::to_string(selfPort), this->GetCreds(), &listeningPort);
    builder.RegisterService(this);
    builder.SetMaxReceiveMessageSize(maxGrpcSize);
    builder.SetMaxSendMessageSize(maxGrpcSize);
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    builder.SetDefaultCompressionLevel(GRPC_COMPRESS_LEVEL_NONE);
    server = builder.BuildAndStart();
    if (server == nullptr) {
        YRLOG_ERROR("Failed to start grpc server, errno: {}, listeningIpAddr: {}, selfPort: {}, listeningPort: {}",
                    errno, listeningIpAddr, selfPort, listeningPort);
        return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, "failed to start grpc server");
    }
    return {};
}

void GrpcPosixService::Stop()
{
    bool expected = false;
    bool exchanged = stopped.compare_exchange_strong(expected, true);
    if (!exchanged) {
        return;
    }
    // make
    fsIntfMgr->Clear();
    YRLOG_INFO("start to stop service of {}. listening({}:{})", instanceID, listeningIpAddr, listeningPort);
    this->timerWorker->Stop();
    if (this->server != nullptr) {
        auto tmout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), {1, 0, GPR_TIMESPAN});
        this->server->Shutdown(tmout);
        this->server->Wait();
    }
    absl::WriterMutexLock lock(&this->disconnectedMu);
    for (auto iter : disconnectCallbackTimers) {
        if (iter.second == nullptr) {
            continue;
        }
        timerWorker->CancelTimer(iter.second);
    }
    disconnectCallbackTimers.clear();
    YRLOG_INFO("service of {}. listening({}:{}) is stopped", instanceID, listeningIpAddr, listeningPort);
}

grpc::Status GrpcPosixService::MessageStream(grpc::ServerContext *context,
                                             grpc::ServerReaderWriter<StreamingMessage, StreamingMessage> *stream)
{
    // guard for this to avoid deconstructed
    [[maybe_unused]] auto raii = shared_from_this();
    if (stopped) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "service was already closed");
    }
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto iter = metadata.find("source_id");
    bool isDirect = iter != metadata.end();
    if (!isDirect) {
        return HandleMessageStreamFromFS(context, stream);
    }
    return HandleDirectStream(context, stream, nullptr);
}

grpc::Status GrpcPosixService::HandleDirectStream(
    grpc::ServerContext *context,
    grpc::ServerReaderWriter<StreamingMessage, StreamingMessage> *stream,
    grpc::ServerReaderWriter<BatchStreamingMessage, BatchStreamingMessage> *batchStream)
{
    if (batchStream == nullptr && stream == nullptr) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "invalid stream.");
    }
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto iter = metadata.find("source_id");
    auto srcInstance = std::string(iter->second.data(), iter->second.size());
    iter = metadata.find("dst_id");
    auto dstInstance = std::string(iter->second.data(), iter->second.size());
    if (dstInstance != instanceID) {
        YRLOG_ERROR("Failed to build stream from {}, instance id is not match. remote expected:{} actual:{}",
                    srcInstance, dstInstance, instanceID);
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "The instance id is not match.");
    }
    auto fs = this->fsIntfMgr->TryGet(srcInstance);
    if (fs != nullptr && fs->Available()) {
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "The runtime " + instanceID + " has already connected to the " + srcInstance);
    }
    auto fsIntf = std::make_shared<FSIntfGrpcServerReaderWriter>(instanceID, srcInstance, runtimeID, context,
                                                                 SteamRW{stream, batchStream});
    fsIntf->RegisterMessageHandler(rtMsgHdlrs);
    this->fsIntfMgr->Emplace(srcInstance, fsIntf);
    StartRead(srcInstance, fsIntf, rtDisconnectedTimeout);
    return grpc::Status::OK;
}

grpc::Status GrpcPosixService::BatchMessageStream(
    grpc::ServerContext *context,
    grpc::ServerReaderWriter<BatchStreamingMessage, BatchStreamingMessage> *stream)
{
    // guard for this to avoid deconstructed
    [[maybe_unused]] auto raii = shared_from_this();
    if (stopped) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "service was already closed");
    }
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto iter = metadata.find("source_id");
    bool isDirect = iter != metadata.end();
    if (!isDirect) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "The instance id is not match.");
    }
    return HandleDirectStream(context, nullptr, stream);
}

void GrpcPosixService::StartDisconnectTimer(const std::string &remote, int disconnectedTimeout)
{
    // The stream between bus ---> runtime is disconnected.
    // after disconnectedTimeout(DISCONNECT_TIMEOUT_MS (900s)), check whether reconnected, if not, call notifyCallback
    // of all reqs.
    auto disconnectCallbackTimer = timerWorker->CreateTimer(disconnectedTimeout, -1, [this, remote]() {
        if (disconnectedCb != nullptr) {
            disconnectedCb(remote);
        }
    });
    {
        absl::WriterMutexLock lock(&this->disconnectedMu);
        disconnectCallbackTimers[remote] = disconnectCallbackTimer;
    }
}

void GrpcPosixService::StopDisconnectTimer(const std::string &remote)
{
    absl::WriterMutexLock lock(&this->disconnectedMu);
    if (auto iter = disconnectCallbackTimers.find(remote); iter != disconnectCallbackTimers.end()) {
        // Reconnected. Should cancel timer of NotifyDisconnected
        timerWorker->CancelTimer(iter->second);
        disconnectCallbackTimers.erase(iter);
    }
}

void GrpcPosixService::StartRead(const std::string &remote, const std::shared_ptr<FSIntfGrpcServerReaderWriter> &fsIntf,
                                 int disconnectedTimeout)
{
    StopDisconnectTimer(remote);
    fsIntf->PreStart();
    if (resendCb != nullptr) {
        resendCb(remote);
    }
    (void)fsIntf->Start();
    fsIntf->Stop();
    (void)StartDisconnectTimer(remote, disconnectedTimeout);
}

grpc::Status GrpcPosixService::HandleMessageStreamFromFS(
    grpc::ServerContext *context, grpc::ServerReaderWriter<StreamingMessage, StreamingMessage> *stream)
{
    // must check instanceID first, avoiding wrong client try to connect
    if (!CompareInstanceID(context)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "The instance id is not match.");
    }

    auto fs = this->fsIntfMgr->GetSystemIntf();
    if (fs != nullptr && fs->Available()) {
        YRLOG_ERROR("The runtime has already connected to function system, there is a new client to connect");
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "The runtime has already connected to the function system");
    }
    bool expected = false;
    bool exchanged = fsConnected.compare_exchange_strong(expected, true);
    if (!exchanged) {
        YRLOG_ERROR("The runtime has already connected to function system, there is a new client to connect");
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "The runtime has already connected to the function system");
    }

    auto fsIntf = std::make_shared<FSIntfGrpcServerReaderWriter>(instanceID, FUNCTION_PROXY, runtimeID, context,
                                                                 SteamRW{stream, nullptr});
    fsIntf->RegisterMessageHandler(fsMsgHdlrs);
    this->fsIntfMgr->UpdateSystemIntf(fsIntf);
    if (!notification->HasBeenNotified()) {
        notification->Notify();
    }
    StartRead(FUNCTION_PROXY, fsIntf, fsDisconnectedTimeout);
    fsConnected.store(false);
    return grpc::Status::OK;
}

}  // namespace Libruntime
}  // namespace YR