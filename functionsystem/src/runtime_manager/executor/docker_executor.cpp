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

#include "docker_executor.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <sstream>
#include <utility>

#include "async/asyncafter.hpp"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/utils/collect_status.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/generate_message.h"
#include "common/utils/time_utils.h"
#include "nlohmann/json.hpp"
#include "runtime_manager/port/port_manager.h"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {

constexpr int64_t DEFAULT_GRACEFUL_SHUTDOWN = 5;
constexpr double DEFAULT_CPU_RESOURCE = 500;
constexpr double DEFAULT_MEMORY_RESOURCE = 500;
const std::string YR_ONLY_STDOUT = "YR_ONLY_STDOUT";
const std::string DEFAULT_DOCKER_API_VERSION = "v1.45";

// HTTP status codes
constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_NO_CONTENT = 204;
constexpr int HTTP_STATUS_NOT_MODIFIED = 304;
constexpr int HTTP_STATUS_NOT_FOUND = 404;
constexpr int HTTP_STATUS_CLIENT_ERROR = 400;  // >= 400 is an error response

// HTTP status code string length ("200".."599")
constexpr size_t HTTP_STATUS_CODE_LEN = 3;

// Resource conversion factors
constexpr int CPU_SHARES_PER_CORE = 1024;       // Docker CPUShares: 1024 = 1 core
constexpr int CPU_RESOURCE_SCALE = 1000;        // CPU resource is in milli-cores (1000 = 1 core)
constexpr int64_t BYTES_PER_MB = 1024 * 1024;    // Docker Memory is in bytes; input is MB


DockerExecutor::DockerExecutor(const std::string &name, const litebus::AID &functionAgentAID)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
    auto dockerHost = litebus::os::GetEnv("DOCKER_HOST");
    if (dockerHost.IsSome() && !dockerHost.Get().empty()) {
        const std::string &host = dockerHost.Get();
        const std::string unixPrefix = "unix://";
        if (host.rfind(unixPrefix, 0) == 0) {
            dockerSocketPath_ = host.substr(unixPrefix.length());
        } else if (host.rfind("tcp://", 0) == 0) {
            YRLOG_WARN("DOCKER_HOST tcp:// endpoint not supported (UDS only), fallback to default socket");
            dockerSocketPath_ = DEFAULT_DOCKER_SOCKET;
        } else {
            dockerSocketPath_ = host;
        }
    } else {
        dockerSocketPath_ = DEFAULT_DOCKER_SOCKET;
    }

    auto dockerApiVer = litebus::os::GetEnv("DOCKER_API_VERSION");
    dockerApiVersion_ = dockerApiVer.IsSome() ? dockerApiVer.Get() : DEFAULT_DOCKER_API_VERSION;
}

void DockerExecutor::Init()
{
    YRLOG_INFO("Start init DockerExecutor, socket={}, apiVersion={}", dockerSocketPath_, dockerApiVersion_);
}

void DockerExecutor::Finalize()
{
    YRLOG_INFO("Start finalize DockerExecutor");
    runtime2portMappings_.clear();
    runtime2containerID_.clear();
    runtimeInstanceInfoMap_.clear();
    Executor::Finalize();
}

void DockerExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
}

std::string DockerExecutor::GetDockerApiPrefix() const
{
    return "/" + dockerApiVersion_;
}

// ---- Docker Engine API communication (same UDS HTTP pattern as SupervisorExecutor) ----

int DockerExecutor::ConnectDockerSocket()
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        YRLOG_ERROR("failed to create UDS socket: {}", std::strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (dockerSocketPath_.length() >= sizeof(addr.sun_path)) {
        YRLOG_ERROR("socket path too long: {}", dockerSocketPath_);
        (void)close(fd);
        return -1;
    }
    (void)strncpy_s(addr.sun_path, sizeof(addr.sun_path), dockerSocketPath_.c_str(), dockerSocketPath_.length());
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        YRLOG_ERROR("failed to connect to Docker socket {}: {}", dockerSocketPath_, std::strerror(errno));
        (void)close(fd);
        return -1;
    }

    YRLOG_DEBUG("connected to Docker socket: {}", dockerSocketPath_);
    return fd;
}

std::string DockerExecutor::BuildDockerHttpRequest(const std::string &method, const std::string &path,
    const std::string &body)
{
    if (path.find_first_of("\r\n") != std::string::npos) {
        YRLOG_ERROR("invalid Docker API path with CRLF, refuse to build request: {}", path);
        return "";
    }
    std::ostringstream oss;
    oss << method << " " << path << " HTTP/1.1\r\n";
    oss << "Host: localhost\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "Content-Length: " << body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

void DockerExecutor::ParseDockerResponse(litebus::Promise<nlohmann::json> promise, std::string response)
{
    auto fail = [&promise](const char *reason) {
        YRLOG_ERROR("{}", reason);
        nlohmann::json errResp = nlohmann::json::object();
        errResp["__http_status"] = 0;
        errResp["__parse_failed"] = true;
        promise.SetValue(errResp);
    };

    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        fail("invalid HTTP response (no header/body separator)");
        return;
    }
    std::string headers = response.substr(0, headerEnd);
    std::string respBody = response.substr(headerEnd + 4);

    size_t statusPos = headers.find(" ");
    if (statusPos == std::string::npos) {
        fail("invalid HTTP response (no status line)");
        return;
    }
    int statusCode = 0;
    try {
        statusCode = std::stoi(headers.substr(statusPos + 1, HTTP_STATUS_CODE_LEN));
    } catch (...) {
        fail("failed to parse HTTP status code");
        return;
    }

    nlohmann::json resp = nlohmann::json::object();
    resp["__http_status"] = statusCode;
    if (statusCode >= HTTP_STATUS_CLIENT_ERROR) {
        YRLOG_ERROR("Docker API error: status={}, body={}", statusCode, respBody);
        try {
            resp["__docker_error"] = nlohmann::json::parse(respBody);
        } catch (...) {
            resp["__docker_error_raw"] = respBody;
        }
    } else if (!respBody.empty()) {
        try {
            YRLOG_DEBUG("Docker API response: {}", respBody);
            auto jsonResp = nlohmann::json::parse(respBody);
            jsonResp["__http_status"] = statusCode;
            promise.SetValue(jsonResp);
            return;
        } catch (std::exception const &e) {
            YRLOG_WARN("non-JSON Docker response (status={}): {}", statusCode, e.what());
        }
    }
    promise.SetValue(resp);
}

litebus::Future<nlohmann::json> DockerExecutor::SendRequestToDocker(const std::string &method,
    const std::string &path, const nlohmann::json &body)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> result = promise.GetFuture();
    int fd = ConnectDockerSocket();
    if (fd < 0) {
        YRLOG_ERROR("failed to connect to Docker socket: {}", dockerSocketPath_);
        nlohmann::json errResp = nlohmann::json::object();
        errResp["__http_status"] = 0;
        errResp["__connect_failed"] = true;
        promise.SetValue(errResp);
        return result;
    }
    std::string fullPath = GetDockerApiPrefix() + path;
    std::string httpRequest = BuildDockerHttpRequest(method, fullPath, body.dump());
    if (ssize_t sent = send(fd, httpRequest.c_str(), httpRequest.length(), 0);
        sent < 0 || static_cast<size_t>(sent) != httpRequest.length()) {
        YRLOG_ERROR("failed to send request to Docker socket: {}", std::strerror(errno));
        (void)close(fd);
        nlohmann::json errResp = nlohmann::json::object();
        errResp["__http_status"] = 0;
        errResp["__send_failed"] = true;
        promise.SetValue(errResp);
        return result;
    }
    // Receive the full response. The request uses Connection: close, so the daemon closes the
    // socket when done; reading until EOF handles both Content-Length and chunked/streaming bodies
    // (e.g. /images/create pull progress) without brittle header parsing.
    std::string response;
    char buf[4096];
    ssize_t received = 0;
    while ((received = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        response.append(buf, static_cast<size_t>(received));
    }
    if (received < 0) {
        YRLOG_ERROR("failed to receive response from Docker socket: {}", std::strerror(errno));
        (void)close(fd);
        nlohmann::json errResp = nlohmann::json::object();
        errResp["__http_status"] = 0;
        errResp["__recv_failed"] = true;
        promise.SetValue(errResp);
        return result;
    }
    (void)close(fd);
    ParseDockerResponse(promise, response);
    return result;
}

// ---- Image management ----

namespace {
bool IsValidImageName(const std::string &image)
{
    if (image.empty()) {
        return false;
    }
    // image reference: [host[:port]/]name[:tag], charset [a-zA-Z0-9._/:@-]
    static const std::string allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._/:@-";
    if (image.find_first_not_of(allowed) != std::string::npos) {
        return false;
    }
    if (image.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

// Reject host paths that would let the container escape or hijack the daemon.
bool IsSafeBindSource(const std::string &path)
{
    if (path.empty() || path == "/") {
        return false;
    }
    static const std::vector<std::string> blocked = {
        "/etc", "/proc", "/sys", "/dev", "/boot", "/var/run/docker.sock", "/run/docker.sock"
    };
    for (const auto &b : blocked) {
        if (path == b || path.rfind(b + "/", 0) == 0) {
            return false;
        }
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return true;
}
}

std::string DockerExecutor::GetRuntimeImage(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &opts = info.deploymentconfig().deployoptions();
    auto rootfsIter = opts.find(CONTAINER_ROOTFS);
    if (rootfsIter != opts.end()) {
        auto image = ParseRootfsImageUrl(rootfsIter->second);
        if (!image.empty()) {
            YRLOG_INFO("{}|{}|using Docker image from rootfs config: {}", info.traceid(), info.requestid(), image);
            return image;
        }
    }
    auto defaultImage = litebus::os::GetEnv("DOCKER_RUNTIME_IMAGE");
    if (defaultImage.IsSome() && !defaultImage.Get().empty()) {
        return defaultImage.Get();
    }
    YRLOG_ERROR("{}|{}|no Docker image specified: set deployOptions[\"rootfs\"] (type=image, imageurl=...) or "
                "DOCKER_RUNTIME_IMAGE env", info.traceid(), info.requestid());
    return "";
}

litebus::Future<Status> DockerExecutor::PullImage(const std::string &image)
{
    if (!IsValidImageName(image)) {
        YRLOG_ERROR("invalid Docker image name, refuse to pull: {}", image);
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "invalid Docker image name");
    }
    YRLOG_INFO("pulling Docker image {}", image);
    auto pullFail = [&image]() {
        return Status(StatusCode::ERR_INNER_COMMUNICATION, fmt::format("failed to pull Docker image {}", image));
    };
    return SendRequestToDocker("POST", "/images/create?fromImage=" + image, nlohmann::json::object())
        .Then([image, pullFail](litebus::Try<nlohmann::json> pullResult) -> litebus::Future<Status> {
            if (!pullResult.IsOK()) {
                YRLOG_ERROR("failed to pull Docker image {}", image);
                return pullFail();
            }
            auto pullResp = pullResult.Get();
            if (pullResp.contains("__http_status")) {
                int pullStatus = pullResp["__http_status"].get<int>();
                if (pullStatus >= HTTP_STATUS_CLIENT_ERROR || pullStatus == 0) {
                    YRLOG_ERROR("failed to pull Docker image {}, status={}", image, pullStatus);
                    return pullFail();
                }
            }
            YRLOG_INFO("successfully pulled Docker image {}", image);
            return Status::OK();
        });
}

litebus::Future<Status> DockerExecutor::EnsureImageExists(const std::string &image)
{
    if (!IsValidImageName(image)) {
        YRLOG_ERROR("invalid Docker image name, refuse to inspect: {}", image);
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "invalid Docker image name");
    }
    YRLOG_INFO("checking if Docker image exists: {}", image);
    return SendRequestToDocker("GET", "/images/" + image + "/json", nlohmann::json::object())
        .Then([this, image](litebus::Try<nlohmann::json> result) -> litebus::Future<Status> {
            // If the inspect request itself failed (e.g. daemon unreachable), try pulling anyway.
            if (!result.IsOK()) {
                YRLOG_WARN("failed to check Docker image {}, attempting pull", image);
                return PullImage(image);
            }
            auto checkResp = result.Get();
            if (checkResp.contains("__connect_failed") || checkResp.contains("__send_failed") ||
                checkResp.contains("__recv_failed") || checkResp.contains("__parse_failed") ||
                (checkResp.contains("__http_status") && checkResp["__http_status"].get<int>() == 0)) {
                YRLOG_ERROR("Docker daemon communication failed while checking image {}", image);
                return Status(StatusCode::ERR_INNER_COMMUNICATION, "Docker daemon communication failed");
            }
            int status = checkResp.value("__http_status", 0);
            if (status == HTTP_STATUS_OK) {
                YRLOG_INFO("Docker image {} already exists", image);
                return Status::OK();
            }
            if (status == HTTP_STATUS_NOT_FOUND) {
                YRLOG_INFO("Docker image {} not found, pulling...", image);
                return PullImage(image);
            }
            YRLOG_ERROR("Docker image check returned unexpected status {} for {}", status, image);
            return Status(StatusCode::ERR_INNER_COMMUNICATION,
                          fmt::format("failed to check Docker image {}, status {}", image, status));
        });
}

// ---- Container lifecycle ----

nlohmann::json DockerExecutor::BuildHostConfig(const ContainerCreateSpec &spec) const
{
    nlohmann::json hostConfig = nlohmann::json::object();
    hostConfig["AutoRemove"] = false;
    hostConfig["NetworkMode"] = "bridge";

    // Binds (volume mounts)
    auto binds = nlohmann::json::array();
    for (const auto &m : spec.bindMounts) {
        binds.push_back(m);
    }
    hostConfig["Binds"] = binds;

    // PortBindings: "containerPort/proto" -> hostPort
    nlohmann::json portBindingsJson = nlohmann::json::object();
    nlohmann::json exposedPorts = nlohmann::json::object();
    for (const auto &pb : spec.portBindings) {
        nlohmann::json binding = nlohmann::json::array();
        binding.push_back({{"HostPort", pb.second}});
        portBindingsJson[pb.first] = binding;
        exposedPorts[pb.first] = nlohmann::json::object();
    }
    hostConfig["PortBindings"] = portBindingsJson;

    // Resources: CPUShares (1024 = 1 core; cpu in milli-cores) and Memory (MB -> bytes)
    double cpu = DEFAULT_CPU_RESOURCE;
    double memory = DEFAULT_MEMORY_RESOURCE;
    if (spec.resources.find("cpu") != spec.resources.end()) {
        cpu = spec.resources.at("cpu");
    }
    if (spec.resources.find("memory") != spec.resources.end()) {
        memory = spec.resources.at("memory");
    }
    nlohmann::json resourcesJson = nlohmann::json::object();
    resourcesJson["CPUShares"] = static_cast<int>(cpu * CPU_SHARES_PER_CORE / CPU_RESOURCE_SCALE);
    resourcesJson["Memory"] = static_cast<int64_t>(memory * BYTES_PER_MB);
    resourcesJson["MemorySwap"] = static_cast<int64_t>(memory * BYTES_PER_MB);  // No swap
    hostConfig["Resources"] = resourcesJson;

    // LogConfig
    nlohmann::json logConfig = nlohmann::json::object();
    logConfig["Type"] = "json-file";
    nlohmann::json logConfigConfig = nlohmann::json::object();
    logConfigConfig["max-size"] = "10m";
    logConfigConfig["max-file"] = "3";
    logConfig["Config"] = logConfigConfig;
    hostConfig["LogConfig"] = logConfig;

    return hostConfig;
}

nlohmann::json DockerExecutor::BuildCreateContainerRequest(const ContainerCreateSpec &spec)
{
    nlohmann::json req = nlohmann::json::object();
    req["Image"] = spec.image;

    auto cmd = nlohmann::json::array();
    for (const auto &c : spec.command) {
        cmd.push_back(c);
    }
    req["Cmd"] = cmd;

    auto env = nlohmann::json::array();
    for (const auto &e : spec.envs) {
        env.push_back(e.first + "=" + e.second);
    }
    req["Env"] = env;

    // ExposedPorts at top level, PortBindings inside HostConfig
    nlohmann::json exposedPorts = nlohmann::json::object();
    for (const auto &pb : spec.portBindings) {
        exposedPorts[pb.first] = nlohmann::json::object();
    }
    req["ExposedPorts"] = exposedPorts;

    req["HostConfig"] = BuildHostConfig(spec);

    if (!spec.workingDir.empty()) {
        req["WorkingDir"] = spec.workingDir;
    }
    return req;
}

litebus::Future<Status> DockerExecutor::StartContainer(const std::string &runtimeID, const std::string &containerID)
{
    YRLOG_INFO("{}|starting Docker container {}", runtimeID, containerID);
    return SendRequestToDocker("POST", "/containers/" + containerID + "/start", nlohmann::json::object())
        .Then([runtimeID, containerID](litebus::Try<nlohmann::json> startResult) -> litebus::Future<Status> {
            if (!startResult.IsOK()) {
                YRLOG_ERROR("{}|Start container {} request failed", runtimeID, containerID);
                return Status(StatusCode::ERR_INNER_COMMUNICATION,
                              fmt::format("failed to start container {}", containerID));
            }
            auto resp = startResult.Get();
            if (resp.contains("__http_status")) {
                int status = resp["__http_status"].get<int>();
                if (status == HTTP_STATUS_NO_CONTENT || status == HTTP_STATUS_OK ||
                    status == HTTP_STATUS_NOT_MODIFIED) {
                    YRLOG_INFO("{}|container {} started successfully", runtimeID, containerID);
                    return Status::OK();
                }
                YRLOG_ERROR("{}|Start container {} failed, status={}", runtimeID, containerID, status);
                return Status(StatusCode::ERR_INNER_COMMUNICATION,
                              fmt::format("failed to start container {}, status {}", containerID, status));
            }
            // No HTTP status field — check for connect/send failure
            if (resp.contains("__connect_failed") || resp.contains("__send_failed")) {
                return Status(StatusCode::ERR_INNER_COMMUNICATION, "Docker daemon connection failed");
            }
            // Empty but no error — treat as success (204 No Content case)
            YRLOG_INFO("{}|container {} started (no explicit status)", runtimeID, containerID);
            return Status::OK();
        });
}

litebus::Future<Status> DockerExecutor::StopContainer(const std::string &containerID, int64_t timeout)
{
    std::string path = "/containers/" + containerID + "/stop?t=" + std::to_string(timeout);
    YRLOG_INFO("stopping Docker container {} with timeout {}s", containerID, timeout);
    return SendRequestToDocker("POST", path, nlohmann::json::object())
        .Then([containerID](litebus::Try<nlohmann::json> stopResult) -> litebus::Future<Status> {
            if (!stopResult.IsOK()) {
                YRLOG_WARN("Stop container {} request failed, will try kill", containerID);
                return Status(StatusCode::FAILED);
            }
            auto resp = stopResult.Get();
            if (resp.contains("__http_status")) {
                int status = resp["__http_status"].get<int>();
                if (status == HTTP_STATUS_NO_CONTENT || status == HTTP_STATUS_OK ||
                    status == HTTP_STATUS_NOT_FOUND) {
                    YRLOG_INFO("container {} stopped", containerID);
                    return Status::OK();
                }
                YRLOG_WARN("Stop container {} returned status {}", containerID, status);
                return Status(StatusCode::FAILED);
            }
            return Status::OK();
        });
}

litebus::Future<Status> DockerExecutor::RemoveContainer(const std::string &containerID, bool force)
{
    std::string path = "/containers/" + containerID + "?force=" + (force ? "true" : "false");
    YRLOG_INFO("removing Docker container {}, force={}", containerID, force);
    return SendRequestToDocker("DELETE", path, nlohmann::json::object())
        .Then([containerID](litebus::Try<nlohmann::json> deleteResult) -> litebus::Future<Status> {
            if (!deleteResult.IsOK()) {
                YRLOG_WARN("Delete container {} request failed", containerID);
                return Status::OK();  // Container may already be gone
            }
            auto resp = deleteResult.Get();
            if (resp.contains("__http_status")) {
                int status = resp["__http_status"].get<int>();
                if (status == HTTP_STATUS_NO_CONTENT || status == HTTP_STATUS_OK ||
                    status == HTTP_STATUS_NOT_FOUND) {
                    YRLOG_INFO("container {} removed", containerID);
                    return Status::OK();
                }
                YRLOG_WARN("Delete container {} returned status {}", containerID, status);
                return Status(StatusCode::ERR_INNER_COMMUNICATION,
                              fmt::format("failed to remove container {}, status={}", containerID, status));
            }
            return Status::OK();
        });
}

void DockerExecutor::CleanupRuntimeState(const std::string &runtimeID)
{
    runtime2containerID_.erase(runtimeID);
    runtimeInstanceInfoMap_.erase(runtimeID);
    auto portIter = runtime2portMappings_.find(runtimeID);
    if (portIter != runtime2portMappings_.end()) {
        PortManager::GetInstance().ReleasePorts(runtimeID);
        runtime2portMappings_.erase(portIter);
    }
}

litebus::Future<Status> DockerExecutor::TerminateContainer(const std::string &runtimeID,
    const std::string &containerID, bool force)
{
    auto infoIter = runtimeInstanceInfoMap_.find(runtimeID);
    int64_t timeout = DEFAULT_GRACEFUL_SHUTDOWN;
    if (infoIter != runtimeInstanceInfoMap_.end()) {
        timeout = infoIter->second.gracefulshutdowntime();
    }
    if (force) {
        timeout = 0;
    }
    YRLOG_INFO("{}|terminate container {} for runtime {}, timeout={}", runtimeID, containerID, runtimeID,
               timeout);

    auto cleanup = [this, runtimeID]() {
        CleanupRuntimeState(runtimeID);
        return Status::OK();
    };
    return StopContainer(containerID, timeout)
        .Then([this, containerID, runtimeID, force, cleanup](const litebus::Future<Status> &stopStatus)
                  -> litebus::Future<Status> {
            if (stopStatus.IsError() || stopStatus.Get().IsError()) {
                YRLOG_WARN("{}|graceful stop failed for container {}, force killing", runtimeID, containerID);
                return SendRequestToDocker("POST", "/containers/" + containerID + "/kill", nlohmann::json::object())
                    .Then([this, containerID, cleanup](litebus::Try<nlohmann::json>) -> litebus::Future<Status> {
                        return RemoveContainer(containerID, true).Then([cleanup](const litebus::Future<Status> &) {
                            return cleanup();
                        });
                    });
            }
            return RemoveContainer(containerID, force).Then([cleanup](const litebus::Future<Status> &) {
                return cleanup();
            });
        });
}

// ---- BuildRuntimeCommands & SetRequestEnvsAndLogsForStart ----

void DockerExecutor::BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt,
    const std::vector<std::string> &buildArgs)
{
    for (const auto &arg : buildArgs) {
        funcRt->add_command(arg);
    }
}

void DockerExecutor::SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req, const Envs &envs,
    const std::string &runtimeID)
{
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    req->mutable_userenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    (*req->mutable_userenvs())[YR_ONLY_STDOUT] = "true";

    std::string stdOut;
    std::string stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

void DockerExecutor::ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr,
    const std::string &runtimeID)
{
    auto path = litebus::os::Join(config_.runtimeLogPath, config_.runtimeStdLogDir);
    if (!litebus::os::ExistPath(path)) {
        YRLOG_WARN("{}|std log path {} not found, try to make dir", runtimeID, path);
        if (!litebus::os::Mkdir(path).IsNone()) {
            YRLOG_WARN("{}|failed to make dir {}, msg: {}", runtimeID, path, litebus::os::Strerror(errno));
            return;
        }
    }

    stdOut = litebus::os::Join(path, fmt::format("{}.out", runtimeID));
    if (!litebus::os::ExistPath(stdOut) && TouchFile(stdOut) != 0) {
        YRLOG_WARN("create std out log file {} failed: {}", stdOut, litebus::os::Strerror(errno));
        return;
    }

    stdErr = litebus::os::Join(path, fmt::format("{}.err", runtimeID));
    if (!litebus::os::ExistPath(stdErr) && TouchFile(stdErr) != 0) {
        YRLOG_WARN("create std err log file {} failed: {}", stdErr, litebus::os::Strerror(errno));
    }
}

// ---- StartInstance / StopInstance / SnapshotRuntime ----

litebus::Future<messages::StartInstanceResponse> DockerExecutor::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    const auto &info = request->runtimeinstanceinfo();
    auto runtimeID = info.runtimeid();
    auto instanceID = info.instanceid();
    std::string language = info.runtimeconfig().language();
    (void)transform(language.begin(), language.end(), language.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    const auto &tlsConfig = info.runtimeconfig().tlsconfig();
    RuntimeFeatures features;
    std::string port;
    if (tlsConfig.enableservermode()) {
        port = tlsConfig.posixport();
    } else {
        port = PortManager::GetInstance().RequestPort(runtimeID);
    }
    if (port.empty()) {
        YRLOG_ERROR("{}|{}|port resource is not available, can not start instanceID({}), runtimeID({})", info.traceid(),
                    info.requestid(), info.instanceid(), runtimeID);
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_PORT_UNAVAILABLE);
    }

    std::vector<std::string> args;
    if (auto status = cmdBuilder_.GetBuildArgs(language, port, request, args); status.IsError()) {
        YRLOG_ERROR("{}|{}|get build args failed, can not start instanceID({}), runtimeID({})", info.traceid(),
                    info.requestid(), info.instanceid(), runtimeID);
        auto response = GenFailStartInstanceResponse(request, status.StatusCode(), status.GetMessage());
        response.mutable_startruntimeinstanceresponse()->set_executortype(
            static_cast<int32_t>(EXECUTOR_TYPE::DOCKER));
        return response;
    }

    if (language.find(PYTHON_LANGUAGE) != std::string::npos) {
        std::string pythonServerPath = "/python/yr/main/yr_runtime_main.py";
        args.insert(args.begin(), { "-u", config_.runtimePath + pythonServerPath });
    }

    runtimeInstanceInfoMap_[runtimeID] = info;

    YRLOG_INFO("{}|{}|begin to start Docker container for runtime({}) instance({})", info.traceid(), info.requestid(),
               runtimeID, instanceID);
    auto future = StartRuntime(request, language, GenerateEnvs(config_, request, port, cardIDs, features), args, port);
    inProgressStarts_[runtimeID] = future;
    return future;
}

std::string DockerExecutor::ResolveExecPath(const std::string &language,
    const messages::RuntimeInstanceInfo &info) const
{
    if (!litebus::strings::StartsWithPrefix(language, PYTHON_LANGUAGE)) {
        return cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
    }
    const auto &options = info.deploymentconfig().deployoptions();
    auto execPathIter = options.find(PARAM_EXEC_PATH);
    std::string execPath =
        (execPathIter != options.end()) ? execPathIter->second : cmdBuilder_.GetExecPath(language);
    YRLOG_INFO("{}|{}|python: {} use execPath: {}", info.traceid(), info.requestid(), language, execPath);
    return execPath;
}

std::vector<std::string> DockerExecutor::BuildBindMounts(const Envs &envs,
    const messages::RuntimeInstanceInfo &info) const
{
    std::vector<std::string> bindMounts;
    auto workingDirIter = envs.posixEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIter != envs.posixEnvs.end() && !workingDirIter->second.empty()) {
        std::string mountPoint = info.container().mountpoint();
        if (!mountPoint.empty()) {
            bindMounts.push_back(workingDirIter->second + ":" + mountPoint);
        }
        for (auto &layer : GenerateLayerPath(info)) {
            std::string target = layer;
            std::replace(target.begin(), target.end(), '/', '-');
            bindMounts.push_back(layer + ":" + litebus::os::Join("/opt", target));
        }
    }

    // User-specified host directory mounts from rootfs JSON (docker -v equivalent).
    const auto &opts = info.deploymentconfig().deployoptions();
    auto rootfsIter = opts.find(CONTAINER_ROOTFS);
    if (rootfsIter != opts.end()) {
        for (const auto &m : ParseRootfsMounts(rootfsIter->second)) {
            if (!IsSafeBindSource(m.source)) {
                YRLOG_WARN("{}|skip unsafe bind source: {}", info.runtimeid(), m.source);
                continue;
            }
            bindMounts.push_back(m.source + ":" + m.target + (m.readonly ? ":ro" : ""));
        }
    }
    return bindMounts;
}

bool DockerExecutor::BuildPortBindings(const messages::RuntimeInstanceInfo &info,
    std::map<std::string, std::string> &portBindings)
{
    // User port forwardings (runtime-external services).
    const auto &deployOpts = info.deploymentconfig().deployoptions();
    auto networkIter = deployOpts.find(CONTAINER_NETWORK);
    if (networkIter == deployOpts.end() || networkIter->second.empty()) {
        return true;
    }
    auto forwardConfigs = ParseForwardPorts(networkIter->second);
    if (forwardConfigs.empty()) {
        return true;
    }
    auto hostPorts = PortManager::GetInstance().RequestPorts(info.runtimeid(),
        static_cast<int>(forwardConfigs.size()));
    if (hostPorts.empty()) {
        YRLOG_ERROR("{}|{}|user port allocation failed for runtime({}), requested {}",
            info.traceid(), info.requestid(), info.runtimeid(), forwardConfigs.size());
        return false;
    }
    if (hostPorts.size() != forwardConfigs.size()) {
        YRLOG_WARN("{}|{}|only {}/{} user ports allocated for runtime({}), partial port forwardings",
            info.traceid(), info.requestid(), hostPorts.size(), forwardConfigs.size(), info.runtimeid());
    }
    nlohmann::json portJson = nlohmann::json::array();
    for (size_t i = 0; i < hostPorts.size(); ++i) {
        std::string pbKey = std::to_string(forwardConfigs[i].containerPort) + "/" +
                             (forwardConfigs[i].protocol == "tcp" ? "tcp" : "udp");
        portBindings[pbKey] = std::to_string(hostPorts[i]);
        portJson.push_back(forwardConfigs[i].protocol + ":" + std::to_string(hostPorts[i]) + ":" +
                           std::to_string(forwardConfigs[i].containerPort));
    }
    runtime2portMappings_[info.runtimeid()] = portJson.dump();
    return true;
}

std::map<std::string, double> DockerExecutor::BuildResources(const messages::RuntimeInstanceInfo &info) const
{
    std::map<std::string, double> resources;
    const auto &runtimeResources = info.runtimeconfig().resources();
    if (runtimeResources.resources().find("cpu") != runtimeResources.resources().end()) {
        resources["cpu"] = runtimeResources.resources().at("cpu").scalar().value();
    }
    if (runtimeResources.resources().find("memory") != runtimeResources.resources().end()) {
        resources["memory"] = runtimeResources.resources().at("memory").scalar().value();
    }
    return resources;
}

std::vector<std::string> DockerExecutor::BuildContainerCommand(const std::string &execPath,
    const runtime::v1::StartRequest &startReq)
{
    std::vector<std::string> command;
    if (!execPath.empty()) {
        command.push_back(execPath);
    }
    for (const auto &cmd : startReq.funcruntime().command()) {
        command.push_back(cmd);
    }
    return command;
}

std::string DockerExecutor::ParseCreateContainerResponse(const nlohmann::json &resp, const std::string &runtimeID)
{
    if (!resp.contains("__http_status")) {
        // request-level failure already logged by SendRequestToDocker path
        return "";
    }
    int status = resp["__http_status"].get<int>();
    if (status >= HTTP_STATUS_CLIENT_ERROR) {
        YRLOG_ERROR("{}|Create container failed, status={}", runtimeID, status);
        return "";
    }
    if (resp.contains("__connect_failed") || resp.contains("__send_failed")) {
        YRLOG_ERROR("{}|Docker daemon connection failed during container creation", runtimeID);
        return "";
    }
    if (!resp.contains("Id") || !resp["Id"].is_string()) {
        YRLOG_ERROR("{}|Create container response missing Id", runtimeID);
        return "";
    }
    std::string containerID = resp["Id"].get<std::string>();
    YRLOG_INFO("{}|Create container success: {}", runtimeID, containerID);
    runtime2containerID_.emplace(runtimeID, containerID);
    return containerID;
}

litebus::Future<messages::StartInstanceResponse> DockerExecutor::StartRuntime(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language, const Envs &envs,
    const std::vector<std::string> &args, const std::string &port)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    std::string execPath = ResolveExecPath(language, info);
    std::string image = GetRuntimeImage(request);
    if (image.empty()) {
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_PARAMS_INVALID,
                                            "no Docker image specified");
    }

    runtime::v1::StartRequest startReq;
    BuildRuntimeCommands(startReq.mutable_funcruntime(), args);
    SetRequestEnvsAndLogsForStart(&startReq, envs, runtimeID);

    std::map<std::string, std::string> containerEnvs(startReq.userenvs().begin(), startReq.userenvs().end());
    containerEnvs.erase(LD_LIBRARY_PATH);

    std::vector<std::string> bindMounts = BuildBindMounts(envs, info);

    std::map<std::string, std::string> portBindings;
    if (!BuildPortBindings(info, portBindings)) {
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_PORT_UNAVAILABLE);
    }

    const auto &deployOpts = info.deploymentconfig().deployoptions();
    auto rootfsIter = deployOpts.find(CONTAINER_ROOTFS);
    std::string workdir = rootfsIter != deployOpts.end() ? ParseRootfsWorkdir(rootfsIter->second) : "";

    auto createBody = BuildCreateContainerRequest(ContainerCreateSpec{
        image, BuildContainerCommand(execPath, startReq), containerEnvs, bindMounts, portBindings,
        BuildResources(info), workdir });

    return StartContainerChain(request, image, createBody, port);
}

litebus::Future<messages::StartInstanceResponse> DockerExecutor::StartContainerChain(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &image,
    const nlohmann::json &createBody, const std::string &port)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();
    // Ensure image -> create container -> start container
    return EnsureImageExists(image)
        .Then([this, runtimeID, image, createBody](const litebus::Future<Status> &imageStatus)
                  -> litebus::Future<std::string> {
            if (imageStatus.IsError() || imageStatus.Get().IsError()) {
                YRLOG_ERROR("{}|failed to ensure image {} exists", runtimeID, image);
                return "";
            }
            return SendRequestToDocker("POST", "/containers/create", createBody)
                .Then([this, runtimeID](litebus::Try<nlohmann::json> createResult) -> std::string {
                    return createResult.IsOK()
                               ? ParseCreateContainerResponse(createResult.Get(), runtimeID)
                               : "";
                });
        })
        .Then([this, request, port](const std::string &containerID)
                  -> litebus::Future<messages::StartInstanceResponse> {
            const auto &info = request->runtimeinstanceinfo();
            const auto &runtimeID = info.runtimeid();
            if (containerID.empty()) {
                YRLOG_ERROR("{}|{}|failed to create Docker container for runtime({})", info.traceid(),
                            info.requestid(), runtimeID);
                CleanupRuntimeState(runtimeID);
                return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED,
                                                    "Failed to create Docker container");
            }
            return StartContainer(runtimeID, containerID)
                .Then(litebus::Defer(GetAID(), &DockerExecutor::OnStartRuntime, std::placeholders::_1, request, port));
        });
}

litebus::Future<messages::StartInstanceResponse> DockerExecutor::OnStartRuntime(
    const Status &startStatus, const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::string &port)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    // StartContainer translates all failures (HTTP error / connect failure) into an error Status,
    // so a non-OK status here means container start failed.
    if (startStatus.IsError()) {
        YRLOG_ERROR("{}|{}|failed to start Docker container: {}", info.traceid(), info.requestid(),
                    startStatus.RawMessage());
        // Remove the created container (if any) and release all per-runtime state.
        auto containerIter = runtime2containerID_.find(runtimeID);
        if (containerIter != runtime2containerID_.end()) {
            RemoveContainer(containerIter->second, true);
        }
        CleanupRuntimeState(runtimeID);
        (void)inProgressStarts_.erase(runtimeID);
        auto msg = fmt::format("Failed to start Docker container: {}", startStatus.RawMessage());
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, msg);
    }

    auto containerIter = runtime2containerID_.find(runtimeID);
    std::string containerID = containerIter != runtime2containerID_.end() ? containerIter->second : "";

    YRLOG_INFO("{}|{}|start Docker container success, runtimeID({}), containerID({})", info.traceid(), info.requestid(),
               runtimeID, containerID);

    auto response = GenSuccessStartInstanceResponse(request, containerID, port);
    return litebus::Async(GetAID(), &DockerExecutor::OnStartInstanceCompleted, runtimeID, response);
}

litebus::Future<messages::StartInstanceResponse> DockerExecutor::OnStartInstanceCompleted(
    const std::string &runtimeID, const messages::StartInstanceResponse &response)
{
    (void)inProgressStarts_.erase(runtimeID);
    if (pendingDeletes_.erase(runtimeID) > 0) {
        YRLOG_INFO("runtime({}) finish start, but has pending delete, start cleaning up", runtimeID);
        if (response.code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
            auto stopReq = std::make_shared<messages::StopInstanceRequest>();
            stopReq->set_runtimeid(runtimeID);
            stopReq->set_requestid("cleanup-" + runtimeID);
            litebus::Async(GetAID(), &DockerExecutor::StopInstance, stopReq, false);
        }
    }
    return response;
}

messages::StartInstanceResponse DockerExecutor::GenSuccessStartInstanceResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &containerID,
    const std::string &port)
{
    messages::StartInstanceResponse response;
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("start instance success");
    auto info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    response.set_requestid(info.requestid());
    response.mutable_startruntimeinstanceresponse()->set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::DOCKER));

    auto instanceResponse = response.mutable_startruntimeinstanceresponse();
    instanceResponse->set_runtimeid(runtimeID);
    instanceResponse->set_containerid(containerID);
    YRLOG_DEBUG("{}|{}|instance({}) runtime({}) with Docker container({})", info.traceid(), info.requestid(),
                info.instanceid(), runtimeID, containerID);

    instanceResponse->set_pid(0);
    instanceResponse->set_address(GetPosixAddress(config_, port));
    auto portMappingsIter = runtime2portMappings_.find(runtimeID);
    if (portMappingsIter != runtime2portMappings_.end()) {
        instanceResponse->set_port(portMappingsIter->second);
    }
    return response;
}

litebus::Future<Status> DockerExecutor::StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                                     bool oomKilled)
{
    auto runtimeID = request->runtimeid();
    YRLOG_INFO("{}|begin to stop Docker container for runtime({})", request->requestid(), runtimeID);

    auto containerIter = runtime2containerID_.find(runtimeID);
    if (containerIter == runtime2containerID_.end()) {
        if (inProgressStarts_.find(runtimeID) != inProgressStarts_.end()) {
            YRLOG_INFO("{}|runtime({}) is starting, mark as pending delete", request->requestid(), runtimeID);
            pendingDeletes_.insert(runtimeID);
            return Status::OK();
        }
        YRLOG_WARN("{}|can not find containerID to stop runtime({})", request->requestid(), runtimeID);
        return Status::OK();
    }
    std::string containerID = containerIter->second;
    return TerminateContainer(runtimeID, containerID, oomKilled);
}

litebus::Future<messages::SnapshotRuntimeResponse> DockerExecutor::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(request->requestid());
    response.set_code(static_cast<int32_t>(StatusCode::GRPC_UNIMPLEMENTED));
    response.set_message("Snapshot is not supported for Docker-based runtime");
    YRLOG_WARN("{}|SnapshotRuntime is not supported for DockerExecutor", request->requestid());
    return response;
}

std::map<std::string, messages::RuntimeInstanceInfo> DockerExecutor::GetRuntimeInstanceInfos()
{
    return runtimeInstanceInfoMap_;
}

litebus::Future<messages::UpdateCredResponse> DockerExecutor::UpdateCredForRuntime(
    const std::shared_ptr<messages::UpdateCredRequest> &request)
{
    auto runtimeID = request->runtimeid();
    YRLOG_INFO("update credentials for runtime({})", runtimeID);

    messages::UpdateCredResponse response;
    response.set_requestid(request->requestid());
    response.set_code(0);
    response.set_message("update credentials success");
    return response;
}

litebus::Future<Status> DockerExecutor::NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                            const int limit)
{
    YRLOG_INFO("notify instances disk usage exceed limit: {}", description);
    return Status::OK();
}

bool DockerExecutor::IsRuntimeActive(const std::string &runtimeID)
{
    return runtime2containerID_.find(runtimeID) != runtime2containerID_.end();
}

litebus::Future<bool> DockerExecutor::StopAllContainers()
{
    std::list<litebus::Future<Status>> futures;
    YRLOG_INFO("{} containers need to stop", runtime2containerID_.size());
    for (auto [runtimeID, containerID] : runtime2containerID_) {
        futures.emplace_back(TerminateContainer(runtimeID, containerID, false));
        YRLOG_INFO("stop runtime {} with container {}", runtimeID, containerID);
    }
    return CollectStatus(futures, "").Then([]() -> litebus::Future<bool> { return true; });
}

// ---- DockerExecutorProxy ----

litebus::Future<::messages::StartInstanceResponse> DockerExecutorProxy::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    return litebus::Async(executor_->GetAID(), &DockerExecutor::StartInstance, request, cardIDs);
}

litebus::Future<Status> DockerExecutorProxy::StopInstance(
    const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled)
{
    return litebus::Async(executor_->GetAID(), &DockerExecutor::StopInstance, request, oomKilled);
}

litebus::Future<messages::SnapshotRuntimeResponse> DockerExecutorProxy::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    return litebus::Async(executor_->GetAID(), &DockerExecutor::SnapshotRuntime, request);
}

litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> DockerExecutorProxy::GetRuntimeInstanceInfos()
{
    return litebus::Async(executor_->GetAID(), &DockerExecutor::GetRuntimeInstanceInfos);
}

}  // namespace functionsystem::runtime_manager
