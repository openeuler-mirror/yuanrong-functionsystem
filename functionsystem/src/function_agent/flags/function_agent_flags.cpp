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
#include "function_agent_flags.h"

#include "common/constants/constants.h"
#include "common/utils/param_check.h"
#include "function_agent/common/constants.h"

namespace functionsystem::function_agent {
const int32_t MIN_FILE_COUNTS = 10;
const int32_t MIN_FILE_SIZE = 10;
const int32_t MAX_FILE_SIZE = 1024 * 1024 * 10;
const int32_t MIN_DIR_DEPTH = 1;
const int32_t MAX_DIR_DEPTH = 50;
const int32_t MAX_CODE_AGING_TIME = 3600;

FunctionAgentFlags::FunctionAgentFlags()
{
    using namespace litebus::flag;
    AddFlag(&FunctionAgentFlags::logConfig, "log_config", "json format string. For log initialization.",
            DEFAULT_LOG_CONFIG);
    AddFlag(&FunctionAgentFlags::nodeID, "node_id", "ID of this node");
    AddFlag(&FunctionAgentFlags::ip, "ip", "IP address to listen on.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&FunctionAgentFlags::localSchedulerAddress, "local_scheduler_address", "local scheduler's address", true,
            FlagCheckWrraper(IsAddressesValid));
    AddFlag(&FunctionAgentFlags::agentListenPort, "agent_listen_port",
            "For agent actor server listening. example: 22799", true, FlagCheckWrraper(IsPortValid));

    AddFlag(&FunctionAgentFlags::fileCountMax, "file_count_max", "maximum number of files when download S3 object",
            function_agent::FILE_COUNTS_MAX, NumCheck(MIN_FILE_COUNTS, FILE_COUNTS_MAX));
    AddFlag(&FunctionAgentFlags::zipFileSizeMaxMB, "zip_file_size_max_MB",
            "the file size threshold when download S3 object, unit: MB", function_agent::ZIP_FILE_SIZE_MAX_MB,
            NumCheck(MIN_FILE_SIZE, MAX_FILE_SIZE));
    AddFlag(&FunctionAgentFlags::unzipFileSizeMaxMB, "unzip_file_size_max_MB",
            "the size threshold of unzipped files, unit: MB", function_agent::UNZIP_FILE_SIZE_MAX_MB,
            NumCheck(MIN_FILE_SIZE, MAX_FILE_SIZE));
    AddFlag(&FunctionAgentFlags::dirDepthMax, "dir_depth_max", "maximum directory depth of unzipped S3 object",
            function_agent::DIR_DEPTH_MAX, NumCheck(MIN_DIR_DEPTH, MAX_DIR_DEPTH));

    AddFlag(&FunctionAgentFlags::credentialType, "credential_type", "S3's credential type", "",
            WhiteListCheck({ "", CREDENTIAL_TYPE_PERMANENT_CREDENTIALS, CREDENTIAL_TYPE_ROTATING_CREDENTIALS }));
    AddFlag(&FunctionAgentFlags::accessKey, "access_key", "access key when get object from S3", "");
    AddFlag(&FunctionAgentFlags::secretKey, "secret_key", "secret key when get object from S3", "");
    AddFlag(&FunctionAgentFlags::s3Endpoint, "s3_endpoint", "S3's endpoint", "");
    AddFlag(&FunctionAgentFlags::s3Protocol, "s3_protocol", "S3's protocol", S3_PROTOCOL_HTTPS,
            WhiteListCheck({ S3_PROTOCOL_HTTPS, S3_PROTOCOL_HTTP }));
    AddFlag(&FunctionAgentFlags::isEnableS3, "s3_enable", "enable to connect s3", DEFAULT_ENABLE_S3);
    AddFlag(&FunctionAgentFlags::decryptAlgorithm, "decrypt_algorithm", "decrypt algorithm, eg: GCM, CBC, NO_CRYPTO",
            function_agent::NO_CRYPTO_ALGORITHM,
            WhiteListCheck({ NO_CRYPTO_ALGORITHM, CBC_CRYPTO_ALGORITHM, GCM_CRYPTO_ALGORITHM }));

    AddFlag(&FunctionAgentFlags::enableMergeProcess, "enable_merge_process",
            "enable function agent and runtime manager merge in the same process", false);
    AddFlag(&FunctionAgentFlags::alias, "alias", "alias of this agent", "");
    AddFlag(&FunctionAgentFlags::agentUID, "agent_uid", "uid to distinguish different agent, eg: pod name", "");
    AddFlag(&FunctionAgentFlags::localNodeID, "local_node_id", "ID of the node contains proxy", "");
    AddFlag(&FunctionAgentFlags::enableSignatureValidation_, "signature_validation", "package signature validation",
            false);
    AddFlag(&FunctionAgentFlags::codeAgingTime_, "code_aging_time", "code aging time", 0,
            NumCheck(0, MAX_CODE_AGING_TIME));
}

FunctionAgentFlags::~FunctionAgentFlags() = default;

}  // namespace functionsystem::function_agent