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

#ifndef RUNTIME_MANAGER_UTILS_VOLUME_MOUNT_H
#define RUNTIME_MANAGER_UTILS_VOLUME_MOUNT_H

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>

#include "common/status/status.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::runtime_manager {
class VolumeMount {
public:
    virtual ~VolumeMount() = default;
    virtual Status MountVolumesForFunction(const messages::FuncMountConfig &mountConfig);
    virtual Status MountVolume(const messages::FuncMount &funcMount);
    virtual Status CheckECSMountStatus(const std::string &hostIP);
    virtual Status ExecMount(const std::string &host, const std::string &mountSharePath,
                            const std::string &localMountPath);
    virtual int Connect(int32_t clientSocket, struct sockaddr_in addr, int addrLen);
};
}

#endif  // RUNTIME_MANAGER_UTILS_VOLUME_MOUNT_H
