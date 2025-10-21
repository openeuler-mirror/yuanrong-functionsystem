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

#ifndef LITEBUS_SET_ENV_API_HPP
#define LITEBUS_SET_ENV_API_HPP

#ifdef __cplusplus
#include <string>
#include "openssl/ssl.h"

extern "C" {
#endif

void LitebusSetPasswdForDecryptingPrivateKeyC(const char *passwdKey, size_t passwdLen);
void LitebusSetMultiPasswdForDecryptingPrivateKeyC(char *sslKey, const char *passwdKey, size_t passwdLen);
int LitebusSetSSLEnvsC(const char *key, const char *value);
int LitebusSetSSLPemKeyEnvsC(EVP_PKEY *pkey);
int LitebusSetSSLPemCertEnvsC(X509 *cert);
int LitebusSetSSLPemCAEnvsC(STACK_OF(X509) *ca);
int LitebusSetMultiSSLEnvsC(const char *sslKey, const char *key, const char *value);

#ifdef __cplusplus
}
namespace litebus::openssl {
    int GetPasswdForDecryptingPrivateKey(char *passwdKey, size_t passwdLen);
    bool SslInitInternal();
    void SslFinalize();
}
#endif

#endif
