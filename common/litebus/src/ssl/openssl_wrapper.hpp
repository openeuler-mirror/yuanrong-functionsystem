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

#ifndef OPENSSL_WRAPPER_HPP
#define OPENSSL_WRAPPER_HPP

#include "openssl/ssl.h"

#include <map>
#include <string>

namespace litebus {
/**
 * A struct to store all decrypt ways
 */
enum DecryptType {
    WITHOUT_DECRYPT = 0,
    OSS_DECRYPT = 1,
    HARES_DECRYPT = 2,
    OSS_DECRYPT_3LAYERS = 3,
    UNKNOWN_DECRYPT = 100,
};

/**
 * A struct to parse all ssl envs
 */
struct SSL_ENVS {
    bool sslEnabled;
    bool verifyCert;
    bool requireCert;
    bool isLoadFromFile;
    std::string caDir;
    std::string caFile;
    std::string certFile;
    std::string keyFile;
    std::string dptDir;
    std::string dptRootFile;
    std::string dptComFile;
    std::string dptKFile;
    DecryptType dptType;
    bool fetchFromEnv;
    EVP_PKEY *pkey { nullptr };
    X509 *cert { nullptr };
    STACK_OF(X509) *ca { nullptr };

    void Init();
    SSL_ENVS();
    ~SSL_ENVS();
};

void FetchSSLConfigFromEnvCA();
void FetchSSLConfigFromEnvDecrypt();
void SetSSLEnvsDecrypt(SSL_ENVS *sslEnvs, const std::string &key, const std::string &value);
void SetSSLEnvsDecrypt(const std::string &key, const std::string &value);
void FetchSSLConfigFromMap(std::map<std::string, std::string> &environment);
void SetPasswdForDecryptingPrivateKey(const char *passwdKey, size_t passwdLen);
int GetPasswdForDecryptingPrivateKey(char *passwdKey, size_t passwdLen);
int ClearPasswdForDecryptingPrivateKey();
}

namespace litebus {
namespace openssl {

/**
 * Initializes the global Openssl context. All input parameters are obtained
 * through the following environment variables:
 * export LITEBUS_SSL_ENABLED=1|0
 * export LITEBUS_SSL_VERIFY_CERT=1|0
 * export LITEBUS_SSL_REQUIRE_CERT=1|0
 * export LITEBUS_SSL_CA_DIR=(CA directory)
 * export LITEBUS_SSL_CA_FILE=(CA file path)
 * export LITEBUS_SSL_CERT_FILE=(certificate file)
 * export LITEBUS_SSL_KEY_FILE=(key file)
 * export LITEBUS_SSL_DECRYPT_DIR=(private key decrypt path)
 * export LITEBUS_SSL_DECRYPT_ROOT_FILE=(private key decrypt file:root file)
 * export LITEBUS_SSL_DECRYPT_COMMON_FILE=(private key decrypt file:common file)
 * export LITEBUS_SSL_DECRYPT_KEY_FILE=(private key decrypt file:key file)
 * export LITEBUS_SSL_DECRYPT_TYPE=(0|1|2)
 */
bool SslInit();

void SslFinalize();

/**
 * Returns global Openssl context
 */
SSL_CTX *SslCtx(const bool &client, const std::string &sslKey);

bool IsSslEnabled();

}    // namespace openssl
}    // namespace litebus
#endif
