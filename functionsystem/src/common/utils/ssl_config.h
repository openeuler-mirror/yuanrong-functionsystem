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

#ifndef COMMON_UTILS_SSL_CONFIG_H
#define COMMON_UTILS_SSL_CONFIG_H

#include "common/common_flags/common_flags.h"
#include "common/utils/sensitive_value.h"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "ssl/ssl_env.hpp"

namespace functionsystem {

const int BUFFER_SIZE_KB = 1024;

struct SSLCertConfig {
    bool isEnable = false;
    bool isMetricsSSLEnable = false;
    std::string certPath;
    std::string rootCertFile;
    std::string certFile;
    std::string keyFile;
};

[[maybe_unused]] static std::string GetRealPath(const std::string &path)
{
    auto res = litebus::os::RealPath(path);
    if (res.IsSome()) {
        return res.Get().c_str();
    }
    return "";
}

[[maybe_unused]] static SSLCertConfig GetSSLCertConfig(const CommonFlags &flags)
{
    SSLCertConfig sslCertConfig;
    if (!flags.GetSslEnable() && !flags.GetMetricsSslEnable()) {
        return sslCertConfig;
    }

    sslCertConfig.certPath = GetRealPath(flags.GetSslBasePath());
    sslCertConfig.rootCertFile = GetRealPath(sslCertConfig.certPath + "/" + flags.GetSslRootFile());
    sslCertConfig.certFile = GetRealPath(sslCertConfig.certPath + "/" + flags.GetSslCertFile());
    sslCertConfig.keyFile = GetRealPath(sslCertConfig.certPath + "/" + flags.GetSslKeyFile());

    if (!FileExists(sslCertConfig.rootCertFile) || !FileExists(sslCertConfig.certFile) ||
        !FileExists(sslCertConfig.keyFile)) {
        YRLOG_ERROR("missing ssl cert files in {}", sslCertConfig.certPath);
        return sslCertConfig;
    }

    sslCertConfig.isEnable = flags.GetSslEnable();
    sslCertConfig.isMetricsSSLEnable = flags.GetMetricsSslEnable();
    return sslCertConfig;
}

[[maybe_unused]] static Status InitLitebusSSLEnv(const SSLCertConfig &sslCertConfig)
{
  if (!sslCertConfig.isEnable) {
    YRLOG_DEBUG("load ssl cert failed, failed set litebus ssl env");
    return Status(StatusCode::FAILED);
  }

  YRLOG_INFO("Litebus SSL configs: Setting configs from files");
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_ENABLED", "1");
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_VERIFY_CERT", "1");
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_DECRYPT_TYPE", "0");
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_CA_FILE", sslCertConfig.rootCertFile.c_str());
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_CA_DIR", sslCertConfig.certPath.c_str());
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_CERT_FILE", sslCertConfig.certFile.c_str());
  (void)LitebusSetSSLEnvsC("LITEBUS_SSL_KEY_FILE", sslCertConfig.keyFile.c_str());
  return Status::OK();
}

[[maybe_unused]] static int Sha256CalculateFile(const char *file, unsigned char *sha256Value, int valueLength)
{
    FILE *fp;
    SHA256_CTX ctx;
    const int size = BUFFER_SIZE_KB + 1;
    char buffer[size] = { 0 };

    if (!file || !sha256Value) {
        YRLOG_ERROR("file or sha256 ptr is empty");
        return -1;
    }

    if (valueLength < SHA256_DIGEST_LENGTH) {
        YRLOG_ERROR("input value length less than ({})", SHA256_DIGEST_LENGTH);
        return -1;
    }

    fp = fopen(file, "rb");
    if (fp == nullptr) {
        YRLOG_ERROR("Can't open file: {}", std::string(file));
        return -1;
    }

    SHA256_Init(&ctx);
    size_t len = 0;
    while ((len = fread(buffer, 1, BUFFER_SIZE_KB, fp)) > 0) {
        SHA256_Update(&ctx, buffer, len);
    }
    SHA256_Final(sha256Value, &ctx);
    fclose(fp);
    return 0;
}

[[maybe_unused]] static int Sha512CalculateFile(const char *file, unsigned char *sha512Value, int valueLength)
{
    FILE *fp;
    SHA512_CTX ctx;
    const int size = BUFFER_SIZE_KB + 1;
    char buffer[size] = { 0 };

    if (!file || !sha512Value) {
        YRLOG_ERROR("file or sha512 ptr is empty");
        return -1;
    }

    if (valueLength < SHA512_DIGEST_LENGTH) {
        YRLOG_ERROR("input value length less than ({})", SHA256_DIGEST_LENGTH);
        return -1;
    }

    fp = fopen(file, "rb");
    if (fp == nullptr) {
        YRLOG_ERROR("Can't open file: {}", std::string(file));
        return -1;
    }

    SHA512_Init(&ctx);
    size_t len = 0;
    while ((len = fread(buffer, 1, BUFFER_SIZE_KB, fp)) > 0) {
        SHA512_Update(&ctx, buffer, len);
    }
    SHA512_Final(sha512Value, &ctx);
    fclose(fp);
    return 0;
}
}  // namespace functionsystem

#endif  // COMMON_UTILS_SSL_CONFIG_H
