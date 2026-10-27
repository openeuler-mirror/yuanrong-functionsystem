/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_TCP_TUNNEL_SERVER_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_TCP_TUNNEL_SERVER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/ssl.h>

#include "common/state_machine/instance_control_view.h"
#include "function_proxy/local_scheduler/instance_control/idle/idle_mgr.h"

namespace functionsystem::local_scheduler {

struct TcpTunnelServerConfig {
    std::string listenIP;
    uint16_t listenPort{ 0 };
    std::string nodeID;
    bool enableTLS{ false };
    uint32_t maxConnections{ 1024 };
    std::string rootCert;
    std::string moduleCert;
    std::string moduleKey;
};

int ResolvePublishedTCPPort(const std::string &portForwardMetadata, int targetPort, std::string &error);

// Parsed frontend tunnel header. Field contract (enforced by
// ParseTunnelRequestHeader):
//   - instanceID / requestID / protocol: required, non-empty, and subject to the
//     same length bound + log-safe charset as the correlation fields (they are
//     logged on every dial/accept path and instanceID feeds the lookup tables).
//   - targetPort: required, 1..65535.
//   - traceID / traceParent (forwarded by the frontend wsproxy tunnel header):
//     optional — an absent/empty value passes unvalidated (headers from older
//     frontends parse unchanged), but a present value must be bounded in length
//     and log-safe. Malformed values reject the whole request.
struct TunnelRequest {
    std::string instanceID;
    std::string requestID;
    int targetPort{ 0 };
    std::string traceID;
    std::string traceParent;
};

// Parses and validates the tunnel request fields from the framed JSON header.
// Returns false with a non-empty error on invalid/unsupported headers; on
// return the out-params are reset first, so a false return never leaves
// stale fields from a previous parse behind.
bool ParseTunnelRequestHeader(const nlohmann::json &header, TunnelRequest &request, std::string &error);

class TcpTunnelServer {
public:
    TcpTunnelServer(TcpTunnelServerConfig config, std::shared_ptr<InstanceControlView> instanceView,
                    std::shared_ptr<IdleMgr> idleMgr);
    ~TcpTunnelServer();

    bool Start();
    void Stop();

private:
    struct ClientSession {
        SSL *ssl{ nullptr };
        int clientFd{ -1 };
        int backendFd{ -1 };
        std::string instanceID;
        std::string requestID;
        bool counted{ false };
    };

    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> completed;
    };

    void AcceptLoop();
    void HandleClient(int clientFd);
    void ReapWorkers(bool waitForAll = false);
    bool ServeClient(ClientSession &session);
    void CloseClient(ClientSession &session);
    bool ConfigureTLS();
    int ResolveHostPort(const std::string &instanceID, int targetPort, std::string &error) const;
    std::string ResolveContainerIP(const std::string &instanceID, std::string &error) const;
    bool ResolveBackendAddress(const TunnelRequest &request, std::string &connectHost, int &backendPort,
                               std::string &error) const;
    bool Relay(SSL *ssl, int clientFd, int backendFd) const;

    TcpTunnelServerConfig config_;
    std::shared_ptr<InstanceControlView> instanceView_;
    std::shared_ptr<IdleMgr> idleMgr_;
    SSL_CTX *sslContext_{ nullptr };
    int listenFd_{ -1 };
    std::atomic<bool> running_{ false };
    std::thread acceptThread_;
    std::mutex clientsMutex_;
    std::unordered_set<int> clients_;
    std::mutex workersMutex_;
    std::vector<Worker> workers_;
};

}  // namespace local_scheduler

#endif
