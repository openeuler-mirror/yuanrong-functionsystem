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

#include "fs_intf_manager.h"

#include "src/libruntime/fsclient/grpc/fs_intf_grpc_client_reader_writer.h"
#include "src/utility/logger/logger.h"

namespace YR {
namespace Libruntime {
std::shared_ptr<FSIntfReaderWriter> FSIntfManager::NewFsIntfClient(const std::string &srcInstance,
                                                                   const std::string &dstInstance,
                                                                   const std::string &runtimeID,
                                                                   const ReaderWriterClientOption &option,
                                                                   ProtocolType type)
{
    auto fsIntfRW = TryGet(dstInstance);
    if (fsIntfRW != nullptr && !fsIntfRW->Available()) {
        return fsIntfRW;
    }
    std::shared_ptr<FSIntfReaderWriter> rw;
    switch (type) {
        case ProtocolType::GRPC: {
            rw =
                std::make_shared<FSIntfGrpcClientReaderWriter>(srcInstance, dstInstance, runtimeID, clientsMgr, option);
            break;
        }
        default:
            YRLOG_WARN("protocol type {} is not supported.", static_cast<int>(type));
            return nullptr;
    }
    return rw;
}

std::shared_ptr<FSIntfReaderWriter> FSIntfManager::Get(const std::string &instanceID)
{
    bool needErase = false;
    std::shared_ptr<FSIntfReaderWriter> intf;
    std::shared_ptr<FSIntfReaderWriter> intfNeedStop;
    {
        absl::ReaderMutexLock lock(&this->mu);
        if (this->rtIntfs.find(instanceID) == this->rtIntfs.end()) {
            return systemIntf;
        }
        intf = rtIntfs[instanceID];
        if (!this->rtIntfs[instanceID]->Available()) {
            intf = systemIntf;
        }
        if (this->rtIntfs[instanceID]->Abnormal()) {
            intfNeedStop = this->rtIntfs[instanceID];
            needErase = true;
            intf = systemIntf;
        }
    }
    if (needErase) {
        intfNeedStop->Stop();
        absl::WriterMutexLock lock(&this->mu);
        this->rtIntfs.erase(instanceID);
    }
    return intf;
}

std::shared_ptr<FSIntfReaderWriter> FSIntfManager::TryGet(const std::string &instanceID)
{
    absl::ReaderMutexLock lock(&this->mu);
    if (this->rtIntfs.find(instanceID) == this->rtIntfs.end()) {
        return nullptr;
    }
    return rtIntfs[instanceID];
}

bool FSIntfManager::Emplace(const std::string &instanceID, const std::shared_ptr<FSIntfReaderWriter> &intf)
{
    std::shared_ptr<FSIntfReaderWriter> intfNeedStop;
    {
        absl::WriterMutexLock lock(&this->mu);
        if (auto iter = this->rtIntfs.find(instanceID); iter != this->rtIntfs.end()) {
            if (iter->second->Available()) {
                YRLOG_ERROR("duplicated intf reader/writer {}", instanceID);
                return false;
            }
            intfNeedStop = iter->second;
        }
        this->rtIntfs[instanceID] = intf;
    }
    if (intfNeedStop) {
        intfNeedStop->Stop();
    }
    return true;
}

void FSIntfManager::Remove(const std::string &instanceID)
{
    std::shared_ptr<FSIntfReaderWriter> intfNeedStop;
    {
        absl::WriterMutexLock lock(&this->mu);
        if (auto iter = this->rtIntfs.find(instanceID); iter != this->rtIntfs.end()) {
            intfNeedStop = iter->second;
        }
        (void)this->rtIntfs.erase(instanceID);
    }
    if (intfNeedStop) {
        intfNeedStop->Stop();
    }
}

void FSIntfManager::Clear()
{
    std::vector<std::shared_ptr<FSIntfReaderWriter>> rtIntfsNeedStop;
    {
        absl::WriterMutexLock lock(&this->mu);
        if (systemIntf) {
            rtIntfsNeedStop.push_back(systemIntf);
            systemIntf = nullptr;
        }
        for (auto iter : rtIntfs) {
            rtIntfsNeedStop.push_back(iter.second);
        }
        rtIntfs.clear();
    }
    for (auto intf : rtIntfsNeedStop) {
        intf->Stop();
    }
}

void FSIntfManager::UpdateSystemIntf(const std::shared_ptr<FSIntfReaderWriter> &intf)
{
    std::shared_ptr<FSIntfReaderWriter> intfNeedStop;
    {
        absl::WriterMutexLock lock(&this->mu);
        if (systemIntf) {
            intfNeedStop = systemIntf;
        }
        systemIntf = intf;
    }
    if (intfNeedStop) {
        intfNeedStop->Stop();
    }
}

std::shared_ptr<FSIntfReaderWriter> FSIntfManager::GetSystemIntf()
{
    absl::ReaderMutexLock lock(&this->mu);
    return systemIntf;
}
}  // namespace Libruntime
}  // namespace YR