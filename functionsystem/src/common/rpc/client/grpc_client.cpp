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

#include "grpc_client.h"

#include "async/option.hpp"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "common/utils/sensitive_value.h"


namespace functionsystem {
litebus::Option<SensitiveValue> GetCertContent(const std::string &filePath, const std::string &decryptTool)
{
    if (!FileExists(filePath)) {
        return litebus::None();
    }
    return SensitiveValue(Read(filePath));
}

TLSConfig GetGrpcTLSConfigFromFiles(const CommonFlags &flags)
{
    const std::string tlsCAFile = flags.GetEtcdSslBasePath() + "/" + flags.GetETCDRootCAFile();
    const std::string tlsCertFile = flags.GetEtcdSslBasePath() + "/" + flags.GetETCDCertFile();
    const std::string tlsKeyFile = flags.GetEtcdSslBasePath() + "/" + flags.GetETCDKeyFile();
    TLSConfig tlsConfig;
    if (!FileExists(tlsCertFile) || !FileExists(tlsKeyFile) || !FileExists(tlsCAFile)) {
        YRLOG_ERROR("failed to read pem cert files");
        return tlsConfig;
    }
    litebus::Option<SensitiveValue> caContent = GetCertContent(tlsCAFile, "");
    litebus::Option<SensitiveValue> certContent = GetCertContent(tlsCertFile, "");
    litebus::Option<SensitiveValue> keyContent = GetCertContent(tlsKeyFile, flags.GetETCDDecryptTool());
    if (caContent.IsNone() || certContent.IsNone() || caContent.Get().Empty() || certContent.Get().Empty()) {
        YRLOG_ERROR("ca or cert file is not exist or empty");
        return tlsConfig;
    }
    tlsConfig.cert = certContent.Get();
    tlsConfig.ca = caContent.Get();
    tlsConfig.targetName = flags.GetETCDTargetNameOverride();
    tlsConfig.privateKey = keyContent.Get();
    return tlsConfig;
}

GrpcSslConfig GetGrpcSSLConfigFromFiles(const CommonFlags &flags)
{
    auto tlsConfig = GetGrpcTLSConfigFromFiles(flags);
    if (tlsConfig.privateKey.Empty() || tlsConfig.cert.Empty() || tlsConfig.ca.Empty()) {
        YRLOG_ERROR("failed to read pem cert files");
        return GrpcSslConfig{};
    }

    ::grpc::SslCredentialsOptions options{ .pem_root_certs = tlsConfig.ca.GetData(),
                                           .pem_private_key = tlsConfig.privateKey.GetData(),
                                           .pem_cert_chain = tlsConfig.cert.GetData() };
    GrpcSslConfig config{ .sslCredentials = ::grpc::SslCredentials(std::move(options)),
                          .targetName = tlsConfig.targetName };
    return config;
}

GrpcSslConfig GetGrpcSSLConfig(const CommonFlags &flags)
{
    auto authType = flags.GetETCDAuthType();
    if (authType == "TLS") {
        return GetGrpcSSLConfigFromFiles(flags);
    }
    return GrpcSslConfig{};
}

TLSConfig GetGrpcTLSConfig(const CommonFlags &flags)
{
    auto authType = flags.GetETCDAuthType();
    if (authType == "TLS") {
        return GetGrpcTLSConfigFromFiles(flags);
    }
    return TLSConfig{};
}

Status GrpcClientActor::Call(const std::function<grpc::Status(grpc::ClientContext *context)> &grpcCallFunc,
                             const ::grpc::string &method, const std::string &addr, const uint32_t &timeoutSeconds)
{
    std::string preMsg = "[" + addr + "," + method + "] ";

    grpc::ClientContext context;
    if (timeoutSeconds > 0) {
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(timeoutSeconds));
    }
    auto s = grpcCallFunc(&context);
    if (!s.ok()) {
        YRLOG_ERROR("{} Send rpc failed: ( {} ) {}", preMsg, std::to_string(s.error_code()), s.error_message());
        return Status(StatusCode::SYNC_GRPC_CALL_ERROR, s.error_message());
    }
    return Status::OK();
}

}  // namespace functionsystem
