/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
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

#pragma once
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "constant.h"

namespace YR {
const int MAX_PASSWD_LENGTH = 100;

/**
 * @struct Config
 * @brief `Init` input parameters for initializing the openYuanRong system.
 * @note When both Config fields and environment variables are set, Config parameters take precedence over environment
 * variables.
 */
struct Config {
    enum Mode {
        LOCAL_MODE = 0,  //!< 本地模式
        CLUSTER_MODE,    //!< 集群模式
        PERF_LOCAL_MODE, //!< 本地性能模式
        INVALID
    };

    /**
     * @brief Deployment model. Required. Supported values: `LOCAL_MODE`: Local mode (single-machine multi-threading),
     * `CLUSTER_MODE`: Cluster mode (multi-machine multi-process).
     */
    Mode mode = Mode::CLUSTER_MODE;
    /**
     * @brief Custom `.so` file paths. Defaults to `services.yaml` if empty.
     */
    std::vector<std::string> loadPaths;
    /**
     * @brief Function ID returned after deploying C++ function. Required in `CLUSTER_MODE`. The corresponding
     * environment variable is `YRFUNCID`.
     */
    std::string functionUrn = "";
    /**
     * @brief Function ID returned after deploying Python function. Optional in `CLUSTER_MODE`. The corresponding
     * environment variable is `YR_PYTHON_FUNCID`.
     */
    std::string pythonFunctionUrn = "";
    /**
     * @brief Function ID returned after deploying Java function. Optional in `CLUSTER_MODE`. The corresponding
     * environment variable is `YR_JAVA_FUNCID`.
     */
    std::string javaFunctionUrn = "";
    /**
     * @brief openYuanRong cluster address. Required in `CLUSTER_MODE`. The corresponding environment variable is
     * `YR_SERVER_ADDRESS`.
     */
    std::string serverAddr = "";
    /**
     * @brief Data system server address within the cluster. Required in `CLUSTER_MODE`. The corresponding environment
     * variable is `YR_DS_ADDRESS`.
     */
    std::string dataSystemAddr = "";
    /**
     * @brief `true`: function-proxy acts as server, `false`: runtime acts as server. Default is `false`.
     */
    bool enableServerMode = false;
    /**
     * @brief Thread pool size. Valid range: `1~64`. If out of range, defaults to CPU cores count.
     * Only used in ParallelFor.
     */
    int threadPoolSize = 0;
    /**
     * @brief Local thread pool size. Required in `LOCAL_MODE`. Valid range: `1-64`. If out of range,
     * defaults to CPU cores count. Default is `10`.
     */
    uint32_t localThreadPoolSize = 10;
    /**
     * @brief Maximum idle time for instances. Instances will be terminated if idle beyond this duration. Unit: seconds.
     * Valid range: `1~3000`. Defaults to `2` if not configured.
     */
    int recycleTime = DEFAULT_RECYCLETIME;
    /**
     * @brief Enable mutual TLS for external clients. Default is `false`.
     */
    bool enableMTLS = false;
    /**
     * @brief Client certificate file path.
     */
    std::string certificateFilePath = "";
    /**
     * @brief Client private key file path.
     */
    std::string privateKeyPath = "";
    /**
     * @brief Server certificate file path.
     */
    std::string verifyFilePath = "";
    /**
     * @brief Server name for TLS.
     */
    std::string serverName = "";
    /**
     * @brief Enable data system authentication. Default is `false`.
     */
    bool enableDsAuth = false;
    /**
     * @brief `true`: Enable data system encryption (requires public/private key configs). Default is `false`.
     */
    bool enableDsEncrypt = false;
    /**
     * @brief The path of worker public key for data system tls authentication, if enableDsEncrypt is ``true`` and the
     * dsPublicKeyContextPath is empty, an exception will be thrown.
     */
    std::string dsPublicKeyContextPath = "";
    /**
     * @brief The path of client public key for data system tls authentication, if enableDsEncrypt is ``true`` and the
     * runtimePublicKeyContextPath is empty, an exception will be thrown.
     */
    std::string runtimePublicKeyContextPath = "";
    /**
     * @brief The path of client private key for data system tls authentication, if enableDsEncrypt is ``true`` and the
     * runtimePrivateKeyContextPath is empty, an exception will be thrown.
     */
    std::string runtimePrivateKeyContextPath = "";
    std::string primaryKeyStoreFile;
    std::string standbyKeyStoreFile;
    /**
     * @brief Limits the maximum number of stateless function instances. Valid range: `1~65536`. Defaults to `-1` if
     * unconfigured. The `Init` interface throws exceptions for invalid values.
     */
    int maxTaskInstanceNum = -1;
    /**
     * @brief Custom path for metrics logs. The corresponding environment variable is `YR_METRICS_LOG_PATH`.
     */
    std::string metricsLogPath = "";
    /**
     * @brief Whether to enable metrics collection. `false` means disabled, `true` means enabled. Only effective within
     * the cluster. Default is `false`. The corresponding environment variable is `YR_ENABLE_METRICS`.
     */
    bool enableMetrics = false;
    uint32_t defaultGetTimeoutSec = 300;  // 0 means never time out
    bool isDriver = true;                // internal use only, user do not set it.
    /**
     * @brief Max concurrent stateless instance creations. Must be > `0`. Default is `100`.
     */
    int maxConcurrencyCreateNum = 100;
    /**
     * @brief Optional. Max single log file size (MB). Default `0` (if `0`, it will ultimately be set to `40`).
     * Negative values throw exceptions. Log rotation every 30s. The corresponding environment variable
     * is `YR_MAX_LOG_SIZE_MB`.
     */
    uint32_t maxLogSizeMb = 0;
    /**
     * @brief Optional. Max rotated log files retained. Default `0` (if `0`, it will ultimately be set to `20`).
     * Oldest files deleted when exceeded. The corresponding environment variable is `YR_MAX_LOG_FILE_NUM`.
     */
    uint32_t maxLogFileNum = 0;
    /**
     * @brief Optional. Compress rotated logs. Default is `true`.
     * The corresponding environment variable is `YR_LOG_COMPRESS`.
     */
    bool logCompress = true;
    /**
     * @brief Optional. Log level: `DEBUG`, `INFO`, `WARN`, `ERROR`. Invalid values default to `INFO`.
     * The corresponding environment variable is `YR_LOG_LEVEL`.
     */
    std::string logLevel = "";
    /**
     * @brief RPC timeout (seconds). Must be >`10`. Default is `1800`.
     */
    std::int32_t rpcTimeout = 30 * 60;
    /**
     * @brief Optional. Client log directory (created if nonexistent).
     */
    std::string logDir = "";
    /**
     * @brief (Deprecated, use `logDir`) Optional. Alternate log directory.
     */
    std::string logPath = "";
    /**
     * @brief Absolute path to openYuanRong function directory (where `service.yaml` resides). Empty by default.
     */
    std::string workingdir = "";
    /**
     * @brief Default namespace of this client's function.
     */
    std::string ns = "";
    /**
     * @brief Custom environment variables for runtime (only `LD_LIBRARY_PATH` supported).
     */
    std::unordered_map<std::string, std::string> customEnvs;
    /**
     * @brief Enable low-reliability mode for stateless instances (improves creation performance in large-scale
     * scenarios).
     */
    bool isLowReliabilityTask = false;
    /**
     * @brief Attach `libruntime` instance to existing instances during initialization (only supports KV APIs).
     * Default is `false`.
     */
    bool attach = false;
};
}  // namespace YR