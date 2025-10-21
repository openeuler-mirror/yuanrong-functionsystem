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

#include "openssl_wrapper.hpp"
#include "actor/buslog.hpp"
#include "utils/os_utils.hpp"

#include "openssl/err.h"
#include "openssl/rand.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#include <cstdio>

#include <pthread.h>
#include <securec.h>
#include <map>
#include <set>
#include <mutex>
#include <sstream>
#include <string>

using litebus::os::ENV_VAR_MAX_LENGTH;
using std::map;
using std::string;

namespace litebus {

SSL_ENVS::~SSL_ENVS()
{
    ca = nullptr;
    cert = nullptr;
    pkey = nullptr;
}

SSL_ENVS::SSL_ENVS()
    : sslEnabled(false),
      verifyCert(false),
      requireCert(false),
      isLoadFromFile(true),
      dptType(WITHOUT_DECRYPT),
      fetchFromEnv(true)
{
}

void SSL_ENVS::Init()
{
    sslEnabled = false;
    verifyCert = false;
    requireCert = false;
    isLoadFromFile = true;
    caDir.clear();
    caFile.clear();
    certFile.clear();
    keyFile.clear();
    dptDir.clear();
    dptRootFile.clear();
    dptComFile.clear();
    dptKFile.clear();
    dptType = WITHOUT_DECRYPT;
    fetchFromEnv = true;
}

// The global variable used to store the password of private key.
constexpr auto PASSWDLEN = 512;
char g_passwdForDecryptingPrivateKey[PASSWDLEN + 1] = { 0 };
std::map<std::string, char*> g_multiPasswdForDecryptingPrivateKey;

constexpr int WITHOUT_DECRYPT_INDEX = 0;
constexpr int OSS_DECRYPT_INDEX = 1;
constexpr int HARES_DECRYPT_INDEX = 2;
constexpr int DEEP = 4;
constexpr int LITEBUS_ERROR_SIZE_TOO_LONG = -2;
constexpr off_t MAX_FILE_SIZE = 100 * 1024 * 1024;    // 100MB
const std::string DEFAULT_SSL = "litebus";

std::mutex g_sslEnvMutex;
static SSL_ENVS *g_sslEnvs = new (std::nothrow) SSL_ENVS();
std::map<std::string, SSL_ENVS> g_multiSSLEnvs;

static const std::map<int, DecryptType>::value_type G_DECRYPTVALUE[] = {
    std::map<int, DecryptType>::value_type(WITHOUT_DECRYPT_INDEX, WITHOUT_DECRYPT),
    std::map<int, DecryptType>::value_type(OSS_DECRYPT_INDEX, OSS_DECRYPT),
    std::map<int, DecryptType>::value_type(HARES_DECRYPT_INDEX, HARES_DECRYPT)
};

std::map<int, DecryptType> g_toDecryptType(G_DECRYPTVALUE,
                                           G_DECRYPTVALUE + sizeof(G_DECRYPTVALUE) / sizeof(G_DECRYPTVALUE[0]));

bool DecodeBoolString(const string &boolString)
{
    return (boolString == "true" || boolString == "1");
}

DecryptType DecodeDecryptTypeString(const string &decryptTypeString)
{
    int decryptValue;
    try {
        decryptValue = std::stoi(decryptTypeString);
    } catch (const std::exception &e) {
        BUSLOG_ERROR("Decode decrypt type failed, error: {}", e.what());
        return UNKNOWN_DECRYPT;
    }

    auto decryptItem = g_toDecryptType.find(decryptValue);
    if (decryptItem == g_toDecryptType.end()) {
        return UNKNOWN_DECRYPT;
    }
    return g_toDecryptType[decryptValue];
}

const char *GetSslEnv(const string &key)
{
    const char *env = getenv(key.c_str());
    if (env != nullptr && strlen(env) <= ENV_VAR_MAX_LENGTH) {
        return env;
    }
    return nullptr;
}

void FetchSSLConfigFromEnvCA()
{
    if (g_sslEnvs == nullptr) {
        return;
    }

    const char *sslEnabledString = GetSslEnv("LITEBUS_SSL_ENABLED");
    if (sslEnabledString != nullptr) {
        g_sslEnvs->sslEnabled = DecodeBoolString(sslEnabledString);
        BUSLOG_INFO("sslEnabled is {}", g_sslEnvs->sslEnabled);
    }

    const char *sslLoadFromFileString = GetSslEnv("LITEBUS_SSL_LOAD_FROM_FILE");
    if (sslLoadFromFileString != nullptr) {
        g_sslEnvs->isLoadFromFile = DecodeBoolString(sslLoadFromFileString);
        BUSLOG_INFO("sslLoadFromFile is {}", g_sslEnvs->isLoadFromFile);
    }

    const char *verifyCertString = GetSslEnv("LITEBUS_SSL_VERIFY_CERT");
    if (verifyCertString != nullptr) {
        g_sslEnvs->verifyCert = DecodeBoolString(verifyCertString);
        BUSLOG_INFO("verifyCert is {}", g_sslEnvs->verifyCert);
    }

    const char *requireCertString = GetSslEnv("LITEBUS_SSL_REQUIRE_CERT");
    if (requireCertString != nullptr) {
        g_sslEnvs->requireCert = DecodeBoolString(requireCertString);
        BUSLOG_INFO("requireCert is {}", g_sslEnvs->requireCert);
    }

    const char *caDirString = GetSslEnv("LITEBUS_SSL_CA_DIR");
    if (caDirString != nullptr) {
        g_sslEnvs->caDir = caDirString;
    }

    const char *caFileString = GetSslEnv("LITEBUS_SSL_CA_FILE");
    if (caFileString != nullptr) {
        g_sslEnvs->caFile = caFileString;
    }

    const char *caCertFileString = GetSslEnv("LITEBUS_SSL_CERT_FILE");
    if (caCertFileString != nullptr) {
        g_sslEnvs->certFile = caCertFileString;
    }

    const char *keyFileString = GetSslEnv("LITEBUS_SSL_KEY_FILE");
    if (keyFileString != nullptr) {
        g_sslEnvs->keyFile = keyFileString;
    }
}

void FetchSSLConfigFromEnvDecrypt()
{
    if (g_sslEnvs == nullptr) {
        return;
    }

    const char *decryptDirString = GetSslEnv("LITEBUS_SSL_DECRYPT_DIR");
    if (decryptDirString != nullptr) {
        g_sslEnvs->dptDir = decryptDirString;
    }

    const char *decryptRootFileString = GetSslEnv("LITEBUS_SSL_DECRYPT_ROOT_FILE");
    if (decryptRootFileString != nullptr) {
        g_sslEnvs->dptRootFile = decryptRootFileString;
    }

    const char *decryptCommonFileString = GetSslEnv("LITEBUS_SSL_DECRYPT_COMMON_FILE");
    if (decryptCommonFileString != nullptr) {
        g_sslEnvs->dptComFile = decryptCommonFileString;
    }

    const char *decryptKeyFileString = GetSslEnv("LITEBUS_SSL_DECRYPT_KEY_FILE");
    if (decryptKeyFileString != nullptr) {
        g_sslEnvs->dptKFile = decryptKeyFileString;
    }

    const char *decryptTypeString = GetSslEnv("LITEBUS_SSL_DECRYPT_TYPE");
    if (decryptTypeString != nullptr) {
        g_sslEnvs->dptType = DecodeDecryptTypeString(decryptTypeString);
        BUSLOG_INFO("dptType is {}", g_sslEnvs->dptType);
    }
}

void FetchSSLConfigFromEnv()
{
    FetchSSLConfigFromEnvCA();
    FetchSSLConfigFromEnvDecrypt();
    return;
}

void SetSSLEnvsCA(SSL_ENVS *sslEnvs, const std::string &key, const std::string &value)
{
    if (sslEnvs == nullptr) {
        return;
    }
    if (key == "LITEBUS_SSL_ENABLED") {
        sslEnvs->sslEnabled = DecodeBoolString(value);
        BUSLOG_INFO("sslEnabled is {}", sslEnvs->sslEnabled);
    } else if (key == "LITEBUS_SSL_VERIFY_CERT") {
        sslEnvs->verifyCert = DecodeBoolString(value);
        BUSLOG_INFO("verifyCert is {}", sslEnvs->verifyCert);
    } else if (key == "LITEBUS_SSL_REQUIRE_CERT") {
        sslEnvs->requireCert = DecodeBoolString(value);
        BUSLOG_INFO("requireCert is {}", sslEnvs->requireCert);
    } else if (key == "LITEBUS_SSL_CA_DIR") {
        sslEnvs->caDir = value;
    } else if (key == "LITEBUS_SSL_CA_FILE") {
        sslEnvs->caFile = value;
    } else if (key == "LITEBUS_SSL_CERT_FILE") {
        sslEnvs->certFile = value;
    } else if (key == "LITEBUS_SSL_KEY_FILE") {
        sslEnvs->keyFile = value;
    } else if (key == "LITEBUS_SSL_LOAD_FROM_FILE") {
        sslEnvs->isLoadFromFile = DecodeBoolString(value);
    }
}

void SetSSLEnvsCA(const std::string &key, const std::string &value)
{
    SetSSLEnvsCA(g_sslEnvs, key, value);
}

void SetSSLEnvsDecrypt(SSL_ENVS *sslEnvs, const std::string &key, const std::string &value)
{
    if (sslEnvs == nullptr) {
        return;
    }
    if (key == "LITEBUS_SSL_DECRYPT_DIR") {
        sslEnvs->dptDir = value;
    } else if (key == "LITEBUS_SSL_DECRYPT_ROOT_FILE") {
        sslEnvs->dptRootFile = value;
    } else if (key == "LITEBUS_SSL_DECRYPT_COMMON_FILE") {
        sslEnvs->dptComFile = value;
    } else if (key == "LITEBUS_SSL_DECRYPT_KEY_FILE") {
        sslEnvs->dptKFile = value;
    } else if (key == "LITEBUS_SSL_DECRYPT_TYPE") {
        sslEnvs->dptType = DecodeDecryptTypeString(value);
        BUSLOG_INFO("dptType is {}", sslEnvs->dptType);
    } else if (key == "LITEBUS_SSL_FETCH_FROM_ENV") {
        sslEnvs->fetchFromEnv = DecodeBoolString(value);
        BUSLOG_INFO("fetchFromEnv is {}", sslEnvs->fetchFromEnv);
    }
}

void SetSSLEnvsDecrypt(const std::string &key, const std::string &value)
{
    SetSSLEnvsDecrypt(g_sslEnvs, key, value);
}

void SetSSLEnvs(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(g_sslEnvMutex);
    SetSSLEnvsCA(key, value);
    SetSSLEnvsDecrypt(key, value);
    return;
}

void SetSSLPemKeyEnvs(SSL_ENVS *sslEnvs, EVP_PKEY *pkey)
{
    std::lock_guard<std::mutex> lock(g_sslEnvMutex);
    if (sslEnvs == nullptr) {
        return;
    }
    sslEnvs->pkey = pkey;
}

void SetSSLPemCertEnvs(SSL_ENVS *sslEnvs, X509 *cert)
{
    std::lock_guard<std::mutex> lock(g_sslEnvMutex);
    if (sslEnvs == nullptr) {
        return;
    }
    sslEnvs->cert = cert;
}

void SetSSLPemCAEnvs(SSL_ENVS *sslEnvs, STACK_OF(X509) *ca)
{
    std::lock_guard<std::mutex> lock(g_sslEnvMutex);
    if (sslEnvs == nullptr) {
        return;
    }
    sslEnvs->ca = ca;
}

void ClearSslPemCerts(SSL_ENVS *sslEnvs)
{
    if (sslEnvs->pkey) {
        EVP_PKEY_free(sslEnvs->pkey);
    }
    if (sslEnvs->cert) {
        X509_free(sslEnvs->cert);
    }
    if (sslEnvs->ca) {
        sk_X509_pop_free(sslEnvs->ca, X509_free);
    }
}

void FetchSSLConfigFromMap(std::map<std::string, std::string> &environment)
{
    for (const auto &envItem : environment) {
        SetSSLEnvs(envItem.first, envItem.second);
    }
    return;
}

/**
 * The first parameter:passwd contains at most 255 characters(255 not contain terminator '\0').
 * Parameter passwd_len is [0, 512]
 */
void SetPasswd(const char *passwdKey, size_t passwdLen, char *dest)
{
    if ((passwdKey == nullptr) || (passwdLen > PASSWDLEN) || (dest == nullptr)) {
        BUSLOG_ERROR("invalid parameters");
        return;
    }
    errno_t rc = strncpy_s(dest, PASSWDLEN + 1, passwdKey, passwdLen);
    if (rc == EOK) {
        return;
    }
    BUSLOG_ERROR("strncpy_s failed, errno = {}", rc);
}

void SetPasswdForDecryptingPrivateKey(const char *passwdKey, size_t passwdLen)
{
    SetPasswd(passwdKey, passwdLen, g_passwdForDecryptingPrivateKey);
}

FILE *OpenDecryptKeyFile(const string &filePath)
{
    char pathCheck[PATH_MAX] = { 0 };

    if (realpath(filePath.c_str(), pathCheck) == nullptr) {
        BUSLOG_ERROR("realpath checking is error");
        return nullptr;
    }

    struct stat buf = { 0 };
    int rtn = stat(pathCheck, &buf);
    if (rtn != 0) {
        BUSLOG_ERROR("file stat failed");
        return nullptr;
    }

    if (buf.st_size > MAX_FILE_SIZE) {
        BUSLOG_ERROR("file size is too big");
        return nullptr;
    }

    return fopen(pathCheck, "r");
}

int GetMultiSslEnv(const std::string &sslKey, SSL_ENVS &sslEnv)
{
    std::lock_guard<std::mutex> lock(g_sslEnvMutex);
    if (sslKey == DEFAULT_SSL) {
        if (g_sslEnvs == nullptr) {
            BUSLOG_ERROR("SSL envs hasn't been initialized yet.");
            return -1;
        }
        sslEnv = *g_sslEnvs;
        return 0;
    }
    if (g_multiSSLEnvs.find(sslKey) == g_multiSSLEnvs.end()) {
        BUSLOG_ERROR("SSL {} envs hasn't been initialized yet.", sslKey.c_str());
        return -1;
    }
    sslEnv = g_multiSSLEnvs[sslKey];
    return 0;
}

int GetPasswd(const std::string &sslKey, char *passwdKey, size_t passwdLen)
{
    BUSLOG_INFO("get ssl parameters");
    if ((passwdKey == nullptr) || (passwdLen <= PASSWDLEN)) {
        BUSLOG_ERROR("invalid parameters {}", sslKey);
        return -1;
    }

    SSL_ENVS sslEnvs;
    auto ret = GetMultiSslEnv(sslKey, sslEnvs);
    if (ret != 0) {
        return ret;
    }

    switch (sslEnvs.dptType) {
        case WITHOUT_DECRYPT:
        case UNKNOWN_DECRYPT: {
            auto pwd = (sslKey == DEFAULT_SSL) ? g_passwdForDecryptingPrivateKey
                                               : g_multiPasswdForDecryptingPrivateKey[sslKey];
            if (pwd == nullptr) {
                BUSLOG_WARN("no passwd for private key for ssl {}", sslKey);
                return 0;
            }
            errno_t rc = strncpy_s(passwdKey, PASSWDLEN + 1, pwd, PASSWDLEN);
            if (rc != EOK) {
                BUSLOG_ERROR("strncpy error, rtn ={}", rc);
                // 1. dest is valid 2. destsz equals to count and both are valid.
                // memset_s will always executes successfully.
                (void)memset_s(passwdKey, PASSWDLEN + 1, 0, PASSWDLEN + 1);
                return -1;
            }
            return 0;
        }
        default:
            return -1;
    }
}

int GetPasswdForDecryptingPrivateKey(char *passwdKey, size_t passwdLen)
{
    return GetPasswd(DEFAULT_SSL, passwdKey, passwdLen);
}

int ClearPasswd(char *passwdForDecryptingPrivateKey)
{
    errno_t rc = memset_s(passwdForDecryptingPrivateKey, PASSWDLEN + 1, 0, PASSWDLEN + 1);
    if (rc == EOK) {
        return rc;
    }

    BUSLOG_ERROR("memset_s failed, errno ={}", rc);
    return rc;
}

int ClearPasswdForDecryptingPrivateKey()
{
    return ClearPasswd(g_passwdForDecryptingPrivateKey);
}

void ClearMultiPasswdForDecryptingPrivateKey()
{
    for (auto iter : g_multiPasswdForDecryptingPrivateKey) {
        (void)ClearPasswd(iter.second);
        delete[] iter.second;
    }
    g_multiPasswdForDecryptingPrivateKey.clear();
}

}

extern "C" {
void LitebusSetPasswdForDecryptingPrivateKeyC(const char *passwdKey, size_t passwdLen)
{
    litebus::SetPasswd(passwdKey, passwdLen, litebus::g_passwdForDecryptingPrivateKey);
    return;
}

void LitebusSetMultiPasswdForDecryptingPrivateKeyC(char *sslKey, const char *passwdKey, size_t passwdLen)
{
    std::string sslKeyS = sslKey;
    if (litebus::g_multiPasswdForDecryptingPrivateKey.find(sslKeyS) !=
        litebus::g_multiPasswdForDecryptingPrivateKey.end()) {
        return;
    }
    char *passwdForDecryptingPrivateKey = new (std::nothrow) char[litebus::PASSWDLEN + 1];
    BUS_OOM_EXIT(passwdForDecryptingPrivateKey);
    litebus::g_multiPasswdForDecryptingPrivateKey[sslKeyS] = passwdForDecryptingPrivateKey;
    litebus::SetPasswd(passwdKey, passwdLen, passwdForDecryptingPrivateKey);
    return;
}

int LitebusSetSSLEnvsC(const char *key, const char *value)
{
    if ((key == nullptr) || (value == nullptr)) {
        return -1;
    }
    if (strlen(value) > ENV_VAR_MAX_LENGTH) {
        return litebus::LITEBUS_ERROR_SIZE_TOO_LONG;
    }
    litebus::SetSSLEnvs(string(key), string(value));
    return 0;
}

int LitebusSetSSLPemKeyEnvsC(EVP_PKEY *pkey)
{
    if (pkey == nullptr) {
        return -1;
    }
    litebus::SetSSLPemKeyEnvs(litebus::g_sslEnvs, pkey);
    return 0;
}

int LitebusSetSSLPemCertEnvsC(X509 *cert)
{
    if (cert == nullptr) {
        return -1;
    }
    litebus::SetSSLPemCertEnvs(litebus::g_sslEnvs, cert);
    return 0;
}

int LitebusSetSSLPemCAEnvsC(STACK_OF(X509) *ca)
{
    if (ca == nullptr) {
        return -1;
    }
    litebus::SetSSLPemCAEnvs(litebus::g_sslEnvs, ca);
    return 0;
}

int LitebusSetMultiSSLEnvsC(const char *sslKey, const char *key, const char *value)
{
    if ((key == nullptr) || (value == nullptr) || (sslKey == nullptr)) {
        return -1;
    }
    if (strlen(value) > ENV_VAR_MAX_LENGTH) {
        return litebus::LITEBUS_ERROR_SIZE_TOO_LONG;
    }
    std::lock_guard<std::mutex> lock(litebus::g_sslEnvMutex);
    if (litebus::g_multiSSLEnvs.find(string(sslKey)) == litebus::g_multiSSLEnvs.end()) {
        litebus::g_multiSSLEnvs[string(sslKey)] = litebus::SSL_ENVS();
    }
    litebus::SetSSLEnvsCA(&litebus::g_multiSSLEnvs[string(sslKey)], string(key), string(value));
    litebus::SetSSLEnvsDecrypt(&litebus::g_multiSSLEnvs[string(sslKey)], string(key), string(value));
    return 0;
}
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
// NOTE: we must define this lock value struct in global namespace
struct CRYPTO_dynlock_value {
    std::mutex mutex;
};
#endif

namespace litebus {
namespace openssl {

static SSL_CTX *g_sslServerCtx = nullptr;
static SSL_CTX *g_sslClientCtx = nullptr;
std::map<std::string, SSL_CTX*> g_sslCtx;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
static std::mutex *g_mutexes = nullptr;
#endif

// Callback for OpenSSL peer certificate verification.
int VerifyCallback(int ret, X509_STORE_CTX *store)
{
    if (ret != 1) {
        std::ostringstream message;
        int error = X509_STORE_CTX_get_error(store);
        const char *errorPtr = X509_verify_cert_error_string(error);
        if (errorPtr != nullptr) {
            message << "Error code is :" << std::to_string(error) << ", with message :" << errorPtr;

            BUSLOG_ERROR("verify err msg is {}", message.str());
        } else {
            BUSLOG_ERROR("verify err msg is can not get the error message");
        }
    }
    return ret;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
void LockFun(int mode, int n, const char *file, int line)
{
    auto cryptoMode = static_cast<unsigned int>(mode);
    if (cryptoMode & CRYPTO_LOCK) {
        g_mutexes[n].lock();
    } else {
        g_mutexes[n].unlock();
    }
}

unsigned long IdFun()
{
    return ((unsigned long)pthread_self());
}

CRYPTO_dynlock_value *DynCreateFun(const char *file, int line)
{
    auto *value = new (std::nothrow) CRYPTO_dynlock_value();

    if (value == nullptr) {
        return nullptr;
    }

    return value;
}

void DynLockFun(int mode, CRYPTO_dynlock_value *value, const char *file, int line)
{
    auto cryptoMode = static_cast<unsigned int>(mode);
    if (cryptoMode & CRYPTO_LOCK) {
        value->mutex.lock();
    } else {
        value->mutex.unlock();
    }
}

void DynKillLockFun(CRYPTO_dynlock_value *value, const char *file, int line)
{
    delete value;
}
#endif

/**
 * Clean SSL context. It is visible only to Tests with explicitly declaring this function as
 * we don't want applications changing the global ssl context on running time.
 */
void SslClean()
{
    if (g_sslClientCtx != nullptr) {
        SSL_CTX_free(g_sslClientCtx);
        g_sslClientCtx = nullptr;
    }

    if (g_sslServerCtx != nullptr) {
        SSL_CTX_free(g_sslServerCtx);
        g_sslServerCtx = nullptr;
    }

    for (auto iter : g_sslCtx) {
        if (iter.second == nullptr) {
            continue;
        }
        SSL_CTX_free(iter.second);
        iter.second = nullptr;
    }
    g_sslCtx.clear();
}

int SslDecryptPrivateKey()
{
    char out[PASSWDLEN + 1] = { 0 };
    if (GetPasswdForDecryptingPrivateKey(out, PASSWDLEN + 1) < 0) {
        return -1;
    }
    SetPasswdForDecryptingPrivateKey(out, PASSWDLEN);
    (void)memset_s(out, PASSWDLEN + 1, 0, PASSWDLEN + 1);
    return 0;
}

int MultiSslDecryptPrivateKey(const std::string &key)
{
    if (key != DEFAULT_SSL &&
        g_multiPasswdForDecryptingPrivateKey.find(key) == g_multiPasswdForDecryptingPrivateKey.end()) {
        char *passwdForDecryptingPrivateKey = new (std::nothrow) char[PASSWDLEN + 1]{ 0 };
        BUS_OOM_EXIT(passwdForDecryptingPrivateKey);
        g_multiPasswdForDecryptingPrivateKey[key] = passwdForDecryptingPrivateKey;
    }
    char out[PASSWDLEN + 1] = { 0 };
    if (GetPasswd(key, out, PASSWDLEN + 1) < 0) {
        return -1;
    }
    auto passwdForDecryptingPrivateKey = g_multiPasswdForDecryptingPrivateKey[key];
    SetPasswd(out, PASSWDLEN, passwdForDecryptingPrivateKey);
    (void)memset_s(out, PASSWDLEN + 1, 0, PASSWDLEN + 1);
    return 0;
}

void SslMutualAuth(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx)
{
    if (sslEnvs->verifyCert) {
        unsigned int mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        SSL_CTX_set_verify(sslCtx, mode, &VerifyCallback);
        SSL_CTX_set_verify_depth(sslCtx, DEEP);
        return;
    }
    SSL_CTX_set_verify(sslCtx, SSL_VERIFY_NONE, nullptr);
}

bool SslVerifyKey(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx)
{
    // Set verify.
    SslMutualAuth(sslEnvs, sslCtx);

    // Validate validity period
    const X509* cert = SSL_CTX_get0_certificate(sslCtx);
    if (!cert) {
        BUSLOG_ERROR("Failed to get certificate");
        return false;
    }
    if (X509_cmp_current_time(X509_get_notBefore(cert)) > 0) {
        BUSLOG_ERROR("The certificate has not yet taken effect");
        return false;
    }
    if (X509_cmp_current_time(X509_get_notAfter(cert)) < 0) {
        BUSLOG_ERROR("The certificate has expired");
        return false;
    }

    // Validate key.
    if (SSL_CTX_check_private_key(sslCtx) != 1) {
        BUSLOG_ERROR("Private key doesn't match the certificate");
        return false;
    }

    // The whitelist opens only encryption algorithms that comply with security specifications.
    const char *modernCiphers =
        "ECDHE-ECDSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:"
        "ECDHE-RSA-CHACHA20-POLY1305:"
        "DHE-RSA-AES128-GCM-SHA256:"
        "!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!3DES";
    if (SSL_CTX_set_cipher_list(sslCtx, modernCiphers) == 0) {
        BUSLOG_ERROR("set cipher fail");
        return false;
    }
    if (SSL_CTX_set_ciphersuites(sslCtx, "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256") == 0) {
        BUSLOG_ERROR("set cipher fail");
        return false;
    }
    return true;
}

bool SetVerifyContextFromPem(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx)
{
    if (sslEnvs->cert == nullptr || sslEnvs->pkey == nullptr || sslEnvs->ca == nullptr) {
        BUSLOG_ERROR("CA, cert or key from pem is empty");
        return false;
    }
    // Set certificate chain.
    if (SSL_CTX_use_certificate(sslCtx, sslEnvs->cert) != 1) {
        BUSLOG_ERROR("Couldn't load cert from pem");
        return false;
    }
    // Set private key.
    if (SSL_CTX_use_PrivateKey(sslCtx, sslEnvs->pkey) != 1) {
        BUSLOG_ERROR("Couldn't load key from pem");
        return false;
    }
    // Set CA CertFile.
    X509_STORE  *caStore = SSL_CTX_get_cert_store(sslCtx);
    if (caStore == nullptr) {
        BUSLOG_ERROR("Couldn't load CA from pem");
        return false;
    }
    for (int i = 0; i < sk_X509_num(sslEnvs->ca); i++) {
        X509 *caCert = sk_X509_value(sslEnvs->ca, i);
        if (!X509_STORE_add_cert(caStore, caCert)) {
            BUSLOG_ERROR("Couldn't load CA Cert from pem");
            return false;
        }
    }
    return SslVerifyKey(sslEnvs, sslCtx);
}

bool SetVerifyContextFromFile(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx)
{
    if (sslEnvs->requireCert && !sslEnvs->verifyCert) {
        sslEnvs->verifyCert = true;
    }
    // Set CA file.
    if (sslEnvs->verifyCert) {
        if (sslEnvs->caFile.empty() || sslEnvs->caDir.empty()) {
            BUSLOG_ERROR("Couldn't load CA file and/or directory");
            return false;
        }
        if (SSL_CTX_load_verify_locations(sslCtx, sslEnvs->caFile.c_str(), sslEnvs->caDir.c_str()) != 1) {
            BUSLOG_ERROR("Couldn't load CA file and/or directory");
            return false;
        }
    }
    // Set certificate chain.
    if (SSL_CTX_use_certificate_chain_file(sslCtx, sslEnvs->certFile.c_str()) != 1) {
        BUSLOG_ERROR("Couldn't load cert file");
        return false;
    }
    // Set private key.
    if (SSL_CTX_use_PrivateKey_file(sslCtx, sslEnvs->keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
        BUSLOG_ERROR("Couldn't load key file");
        return false;
    }
    return SslVerifyKey(sslEnvs, sslCtx);
}

bool SslVerify(SSL_ENVS *sslEnvs, SSL_CTX *sslCtx, char *passwdForDecryptingPrivateKey)
{
    unsigned long sslOptions = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
#ifdef SSL_OP_NO_RENEGOTIATION
    sslOptions |= SSL_OP_NO_RENEGOTIATION;
#endif
#endif
    (void)SSL_CTX_set_options(sslCtx, sslOptions);
    if (SSL_CTX_set_min_proto_version(sslCtx, TLS1_2_VERSION) != 1) {
        BUSLOG_ERROR("Set TLS 1.2 version failed.");
        return false;
    }
    // Set password
    SSL_CTX_set_default_passwd_cb_userdata(sslCtx, passwdForDecryptingPrivateKey);

    // For ECDHE-RSA-AES128-GCM-SHA256, set curve for ssl ctx.
    int groupList[] = {NID_X25519};
    if (SSL_CTX_set1_groups(sslCtx, groupList, static_cast<int>(sizeof(groupList) / sizeof(int))) != 1) {
        BUSLOG_ERROR("Create curve (x25519) fail");
        return false;
    }
    BUSLOG_INFO("Create curve (x25519) success");

    // load certificates from sts pem
    if (!sslEnvs->isLoadFromFile) {
        return SetVerifyContextFromPem(sslEnvs, sslCtx);
    }
    // load certificates from files
    return SetVerifyContextFromFile(sslEnvs, sslCtx);
}

bool SslMultiInit(const std::map<std::string, SSL_ENVS> &multiSSLEnv)
{
    for (auto iter : multiSSLEnv) {
        auto key = iter.first;
        auto sslEnv = iter.second;
        if (!sslEnv.sslEnabled) {
            continue;
        }
        g_sslCtx[key] = SSL_CTX_new(TLS_client_method());
        if (g_sslCtx[key] == nullptr) {
            BUSLOG_ERROR("Couldn't create SSL context {}", key);
            return false;
        }
        // Set the password into global var as 'g_multiPasswdForDecryptingPrivateKey'
        if (MultiSslDecryptPrivateKey(key) < 0) {
            return false;
        }
        (void)SSL_CTX_set_mode(g_sslCtx[key], SSL_MODE_RELEASE_BUFFERS);
        // Set passward and certificate for SSL Context
        (void)SslVerify(&sslEnv, g_sslCtx[key], g_multiPasswdForDecryptingPrivateKey[key]);
        continue;
    }
    return true;
}

bool IsSslEnable()
{
    auto litebusInternalEnable = (g_sslEnvs != nullptr) && (g_sslEnvs->sslEnabled);
    for (auto sslEnv : g_multiSSLEnvs) {
        litebusInternalEnable = litebusInternalEnable || sslEnv.second.sslEnabled;
    }
    return litebusInternalEnable;
}

bool SslParamCheck(const SSL_ENVS &sslEnvs)
{
    if (!sslEnvs.sslEnabled) {
        return true;
    }

    if (!sslEnvs.isLoadFromFile) {
        if (sslEnvs.cert == nullptr || sslEnvs.pkey == nullptr || sslEnvs.ca == nullptr) {
            BUSLOG_ERROR("SSL is load from pem! Plese set path with P12 certificate");
            return false;
        }
        return true;
    }

    if (sslEnvs.keyFile.empty()) {
        BUSLOG_ERROR("SSL requires key! Plese set path with LITEBUS_SSL_KEY_FILE");
        return false;
    }

    if (sslEnvs.certFile.empty()) {
        BUSLOG_ERROR("SSL requires certificate! Plese set path with LITEBUS_SSL_CERT_FILE");
        return false;
    }

    if (sslEnvs.dptType == UNKNOWN_DECRYPT) {
        BUSLOG_ERROR("SSL requires decrypt type! Plese set path with LITEBUS_SSL_DECRYPT_TYPE");
        return false;
    }
    return true;
}
/**
 * Re-configure SSL context. It is visible only to Tests with explicitly declaring this function as
 * we don't want applications changing the global ssl context on running time.
 */
bool SslInitInternal()
{
    litebus::g_sslEnvMutex.lock();
    if (g_sslEnvs == nullptr) {
        BUSLOG_ERROR("SSL envs hasn't been initialized yet.");
        litebus::g_sslEnvMutex.unlock();
        return false;
    }

    SSL_ENVS sslEnvs = *g_sslEnvs;
    std::map<std::string, SSL_ENVS> multiSSLEnv = g_multiSSLEnvs;
    litebus::g_sslEnvMutex.unlock();

    // before Re-configure, clean up all allocated structures
    SslClean();

    // Initialize OpenSSL.
    (void)SSL_library_init();

    (void)SSL_load_error_strings();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    // Prepare mutexes for threading callbacks.
    if (CRYPTO_num_locks() <= 0) {
        return false;
    } else {
        g_mutexes = new (std::nothrow) std::mutex[CRYPTO_num_locks()];
        if (g_mutexes == nullptr) {
            BUSLOG_ERROR("Couldn't allocate memory for mutex.");
            return false;
        }
    }

    // Install functions for muti-thread support. In litebus we may access SSL context in both
    // http client evloop thread and server evloop thread. Note one:CRYPTO_THREADID and associated
    // functions were introduced in OpenSSL 1.0.0  to replace (actually, deprecate) the
    // CRYPTO_set_id_callback().
    CRYPTO_set_id_callback(&IdFun);
    CRYPTO_set_locking_callback(&LockFun);
    CRYPTO_set_dynlock_create_callback(&DynCreateFun);
    CRYPTO_set_dynlock_lock_callback(&DynLockFun);
    CRYPTO_set_dynlock_destroy_callback(&DynKillLockFun);
#endif
    if (!sslEnvs.sslEnabled) {
        return SslMultiInit(multiSSLEnv);
    }

    // create SSL Content Text for client and server
    g_sslServerCtx = SSL_CTX_new(TLS_server_method());
    g_sslClientCtx = SSL_CTX_new(TLS_client_method());
    if (g_sslServerCtx == nullptr || g_sslClientCtx == nullptr) {
        BUSLOG_ERROR("Couldn't create SSL context");
        return false;
    }

    // Set the password into global var as 'g_passwdForDecryptingPrivateKey'
    if (SslDecryptPrivateKey() < 0) {
        return false;
    }

    (void)SSL_CTX_set_mode(g_sslServerCtx, SSL_MODE_RELEASE_BUFFERS);
    (void)SSL_CTX_set_mode(g_sslClientCtx, SSL_MODE_RELEASE_BUFFERS);

    // Set passward and certificate for SSL Context
    if (!SslVerify(&sslEnvs, g_sslServerCtx, g_passwdForDecryptingPrivateKey) ||
        !SslVerify(&sslEnvs, g_sslClientCtx, g_passwdForDecryptingPrivateKey)) {
        return false;
    }

    return SslMultiInit(multiSSLEnv);
}

void SslFinalize()
{
    std::lock_guard<std::mutex> lock(litebus::g_sslEnvMutex);
    if (g_sslEnvs != nullptr) {
        g_sslEnvs->Init();
    }
    for (auto iter : g_multiSSLEnvs) {
        iter.second.Init();
    }
    g_multiSSLEnvs.clear();
}

/**
 * it should be initialized only once in litebus::initialize as it is not threadsafe
 */
bool SslInit()
{
    if (g_sslServerCtx != nullptr || g_sslClientCtx != nullptr) {
        BUSLOG_WARN("ssl Ctx is already initialized");
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(litebus::g_sslEnvMutex);
        if (g_sslEnvs == nullptr) {
            BUSLOG_ERROR("g_sslEnvs initialized failed");
            return false;
        }

        if (g_sslEnvs->fetchFromEnv) {
            FetchSSLConfigFromEnv();
        }

        if (!IsSslEnable()) {
            return true;
        }

        if (!SslParamCheck(*g_sslEnvs)) {
            return false;
        }
    }

    // begin to config SSL context
    bool initialized = SslInitInternal();
    (void)ClearPasswdForDecryptingPrivateKey();
    ClearMultiPasswdForDecryptingPrivateKey();
    // clear certificates from pem
    if (!g_sslEnvs->isLoadFromFile) {
        ClearSslPemCerts(g_sslEnvs);
    }
    if (!initialized) {
        BUSLOG_ERROR("SSL initialize failed");
        SslClean();
        return false;
    }
    BUSLOG_INFO("SSL initialized successfully");
    return true;
}

SSL_CTX *SslCtx(const bool &client, const std::string &sslKey)
{
    if (!sslKey.empty() && sslKey != DEFAULT_SSL) {
        return g_sslCtx[sslKey];
    }
    if (client) {
        return g_sslClientCtx;
    } else {
        return g_sslServerCtx;
    }
}

bool IsSslEnabled()
{
    std::lock_guard<std::mutex> lock(litebus::g_sslEnvMutex);
    if (g_sslEnvs == nullptr) {
        return false;
    }
    bool isEnabled = g_sslEnvs->sslEnabled;
    return isEnabled;
}
}    // namespace openssl
}    // namespace litebus
