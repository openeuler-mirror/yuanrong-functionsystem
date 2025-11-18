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

#include "volume_mount.h"

#include "common/logs/logging.h"
#include "common/utils/param_check.h"
#include "securec.h"
#include "utils/os_utils.hpp"
#include "utils/string_utils.hpp"

namespace functionsystem::runtime_manager {

const std::string MOUNT_STATUS_DISABLED = "disabled";
const std::string MOUNT_TYPE_ECS = "ecs";
const int32_t SHARE_PATH_SPLIT_SIZE = 2;
const int32_t ECS_NFS_PORT = 111;
const int32_t MOUNT_OPTS_TIMEOUT = 240;

Status VolumeMount::MountVolumesForFunction(const messages::FuncMountConfig &mountConfig)
{
    auto &funcMounts = mountConfig.funcmounts();
    if (funcMounts.empty()) {
        YRLOG_DEBUG("empty function mounts, skip mounting nfs volumes.");
        return Status::OK();
    }

    int32_t count = 0;
    for (const auto &funcMount : funcMounts) {
        if (funcMount.status() == MOUNT_STATUS_DISABLED) {
            YRLOG_WARN("mount resource {} not ready", funcMount.mountsharepath());
            if (litebus::os::ExistPath(funcMount.localmountpath())) {
                YRLOG_WARN("local path({}) already exists, will be umount first.", funcMount.localmountpath());
                (void)umount(funcMount.localmountpath().c_str());
            }
            continue;
        }

        if (litebus::os::ExistPath(funcMount.localmountpath())) {
            YRLOG_WARN("local path({}) already exists, skip mounting.", funcMount.localmountpath());
            continue;
        }

        count++;
        if (auto mountResult = MountVolume(funcMount); !mountResult.IsOk()) {
            return mountResult;
        }
    }
    YRLOG_INFO("succeed to mount {} volumes for current function", std::to_string(count));
    return Status::OK();
}

Status VolumeMount::MountVolume(const messages::FuncMount &funcMount)
{
    auto mkdirRes = litebus::os::Mkdir(funcMount.localmountpath());
    if (!mkdirRes.IsNone()) {
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED,
                      "failed to mkdir local path " + funcMount.localmountpath() +
                          ", res: " + std::to_string(mkdirRes.Get()) + ", msg: " + litebus::os::Strerror(errno));
    }

    auto mountSharePath = funcMount.mountsharepath();
    auto hostAndPath = litebus::strings::Split(mountSharePath, ":");
    if (hostAndPath.size() < SHARE_PATH_SPLIT_SIZE) {
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "invalid mount share path: " + mountSharePath);
    }
    auto host = hostAndPath[0];

    if (funcMount.mounttype() == MOUNT_TYPE_ECS) {
        if (auto ecsStatus = CheckECSMountStatus(host); !ecsStatus.IsOk()) {
            return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "failed to check ECS status of resource " +
                                                                               funcMount.mountsharepath() +
                                                                               ", status: " + ecsStatus.ToString());
        }
    }

    YRLOG_INFO("start mounting share dir {} to local dir {}", funcMount.mountsharepath(), funcMount.localmountpath());
    return ExecMount(host, funcMount.mountsharepath(), funcMount.localmountpath());
}

Status VolumeMount::CheckECSMountStatus(const std::string &hostIP)
{
    YRLOG_INFO("input server's IP: {}", hostIP);
    if (!IsIPValid(hostIP)) {
        YRLOG_ERROR("input server's IP({}) is invalid", hostIP);
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "invalid server's IP: " + hostIP);
    }
    YRLOG_INFO("start to detect connection to ECS nfs server");
    auto address = hostIP + ":" + std::to_string(ECS_NFS_PORT);
    // create socket to nfs server
    int32_t clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "failed to create socket");
    }
    struct sockaddr_in addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));

    addr.sin_family = AF_INET;            // Internet address family
    addr.sin_port = htons(ECS_NFS_PORT);  // nfs server's port
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (inet_aton(hostIP.c_str(), &(addr.sin_addr)) == 0) {  // set nfs server's IP
        YRLOG_ERROR("input server's IP({}) is invalid", hostIP);
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "invalid server's IP: " + hostIP);
    }

    int addrLen = sizeof(addr);
    int listenSocket = Connect(clientSocket, addr, addrLen);  // connect to server
    if (listenSocket == -1) {
        (void)close(clientSocket);
        YRLOG_ERROR("cannot connect to server({})", address);
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "failed to connect to " + address);
    }

    (void)close(clientSocket);
    (void)close(listenSocket);

    return Status::OK();
}

Status VolumeMount::ExecMount(const std::string &host, const std::string &mountSharePath,
                              const std::string &localMountPath)
{
    // get host IP by host name
    struct hostent *p = gethostbyname(host.c_str());
    if (p == nullptr) {
        return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, "failed to analise IP");
    }
    char **pptr = p->h_addr_list;
    char str[32] = { '0' };
    for (; *pptr != nullptr; pptr++) {
        auto addr = inet_ntop(p->h_addrtype, *pptr, str, sizeof(str));
        if (addr == nullptr) {
            YRLOG_WARN("failed to mount share path({}) to local path({}) because of null address", mountSharePath,
                       localMountPath);
            continue;
        }
        std::string opts = "addr=" + std::string(addr) + ",vers=3,nolock,timeo=" + std::to_string(MOUNT_OPTS_TIMEOUT);
        int32_t mountResult = mount(mountSharePath.c_str(), localMountPath.c_str(), "nfs", MS_NOSUID, opts.c_str());
        if (mountResult == 0) {
            YRLOG_INFO("succeed to mount share path({}) to local path({}) at resolved address({})", mountSharePath,
                       localMountPath, addr);
            return Status::OK();
        }
        YRLOG_WARN("failed to mount share path({}) to local path({}) at resolved address({}), error number: {}",
                   mountSharePath, localMountPath, addr, std::to_string(mountResult));
    }
    YRLOG_ERROR("failed to mount share path({}) to local path({}) at all resolved addresses.", mountSharePath,
                localMountPath);
    return Status(StatusCode::RUNTIME_MANAGER_MOUNT_VOLUME_FAILED,
                  "failed to mount share path " + mountSharePath + " to local path" + localMountPath);
}

int VolumeMount::Connect(int32_t clientSocket, struct sockaddr_in addr, int addrLen)
{
    return connect(clientSocket, reinterpret_cast<sockaddr *>(&addr), addrLen);
}

}  // namespace functionsystem::runtime_manager
