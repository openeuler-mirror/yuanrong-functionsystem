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

#ifndef COMMON_UTILS_CERTS_UTILS_H
#define COMMON_UTILS_CERTS_UTILS_H

#include <grpcpp/grpcpp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <securec.h>

#include <climits>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/sensitive_value.h"
#include "utils/os_utils.hpp"

namespace functionsystem {
struct PemCerts {
    EVP_PKEY *pkey = nullptr;
    X509 *cert = nullptr;
    STACK_OF(X509) *ca = nullptr;
};

struct TLSConfig {
    SensitiveValue ca;
    SensitiveValue cert;
    SensitiveValue privateKey;
    std::string targetName;
};

[[maybe_unused]] static void ClearPemCerts(PemCerts &pemCerts)
{
    if (pemCerts.pkey) {
        EVP_PKEY_free(pemCerts.pkey);
    }
    if (pemCerts.cert) {
        X509_free(pemCerts.cert);
    }
    if (pemCerts.ca) {
        sk_X509_pop_free(pemCerts.ca, X509_free);
    }
}

[[maybe_unused]] static void ClearP12(PKCS12 *p12)
{
    if (p12) {
        PKCS12_free(p12);
    }
}

[[maybe_unused]] static SensitiveValue GetPrivateKey(EVP_PKEY *pkey)
{
    if (!pkey) {
        YRLOG_WARN("failed to get pkey from EVP_PKEY, empty pkey");
        return {};
    }

    BIO *mem = BIO_new(BIO_s_mem());
    (void)PEM_write_bio_PrivateKey(mem, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char *data;
    auto len = BIO_get_mem_data(mem, &data);
    auto result = SensitiveValue(data, len);
    // clear
    if (int ret = memset_s(data, len, 0, len); ret != 0) {
        YRLOG_WARN("data memset 0 failed, ret: {}", ret);
    }
    (void)BIO_free(mem);
    return result;
}

[[maybe_unused]] static std::string GetCert(X509 *cert)
{
    if (!cert) {
        YRLOG_WARN("failed to get cert from X509, empty cert");
        return "";
    }

    BIO *mem = BIO_new(BIO_s_mem());
    (void)PEM_write_bio_X509(mem, cert);
    char *data;
    long len = BIO_get_mem_data(mem, &data);
    std::string result = std::string(data, len);
    (void)BIO_free(mem);
    return result;
}

[[maybe_unused]] static std::string GetCa(const stack_st_X509 *ca)
{
    if (!ca || sk_X509_num(ca) == 0) {
        YRLOG_WARN("failed to get ca from stack_st_X509, empty ca");
        return "";
    }

    std::string result = "";
    for (int i = 0; i < sk_X509_num(ca); i++) {
        X509 *x = sk_X509_value(ca, i);
        BIO *mem = BIO_new(BIO_s_mem());
        (void)PEM_write_bio_X509(mem, x);
        char *data;
        const long len = BIO_get_mem_data(mem, &data);
        result += std::string(data, len);
        (void)BIO_free(mem);
    }
    return result;
}

[[maybe_unused]] static std::string GetAltNameDNSFromCert(X509 *cert)
{
    if (!cert) {
        YRLOG_WARN("failed to get altNameDns from X509, empty cert");
        return "";
    }

    GENERAL_NAMES *names = nullptr;
    unsigned char *mem = nullptr;

    // get dns from subject alt name
    std::string result = "";
    names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    for (int i = 0; i < sk_GENERAL_NAME_num(names); i++) {
        const GENERAL_NAME *entry = sk_GENERAL_NAME_value(names, i);
        if (entry->type != GEN_DNS) {
            continue;
        }
        int length = ASN1_STRING_to_UTF8(&mem, entry->d.dNSName);
        if (length >= 0) {
            result = std::string((char *)(mem));
            break;
        }
    }
    OPENSSL_free(mem);
    sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
    return result;
}

[[maybe_unused]] static Status GetPemFromP12(const std::string &p12FilePath, const SensitiveValue &password,
                                             PemCerts &pemCerts)
{
    (void)OpenSSL_add_all_algorithms();
    (void)ERR_load_crypto_strings();

    char path[PATH_MAX + 1] = { 0x00 };
    if (p12FilePath.length() > PATH_MAX || realpath(p12FilePath.c_str(), path) == nullptr) {
        YRLOG_ERROR("failed to open p12 cert file({}) because path invalidation fails", p12FilePath);
        return Status(FAILED);
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        YRLOG_ERROR("failed to open p12 cert file: {}", p12FilePath);
        return Status(FAILED);
    }

    PKCS12 *p12 = d2i_PKCS12_fp(fp, nullptr);
    (void)fclose(fp);
    if (!p12) {
        YRLOG_ERROR("invalid p12 cert file");
        return Status(FAILED);
    }

    if (auto ret = PKCS12_parse(p12, password.GetData(), &pemCerts.pkey, &pemCerts.cert, &pemCerts.ca); !ret) {
        ClearP12(p12);
        YRLOG_ERROR("failed to parse p12 file, error code: {}", ret);
        return Status(FAILED);
    }
    ClearP12(p12);
    return Status(SUCCESS);
}

[[maybe_unused]] static X509 *GetCertFromFile(const std::string &certFile)
{
    auto certOpt = litebus::os::RealPath(certFile);
    if (certOpt.IsNone()) {
        YRLOG_ERROR("invalid cert file path {}", certFile);
        return nullptr;
    }
    FILE *fp = fopen(certOpt.Get().c_str(), "r");
    if (!fp) {
        YRLOG_ERROR("unable to open cert {}", certFile);
        return nullptr;
    }

    X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    if (!cert) {
        YRLOG_ERROR("unable to parse certificate in {}", certFile);
        (void)fclose(fp);
        return nullptr;
    }
    (void)fclose(fp);
    return cert;
}

[[maybe_unused]] static EVP_PKEY *GetPrivateKeyFromFile(const std::string &keyFile, const SensitiveValue &password)
{
    auto keyOpt = litebus::os::RealPath(keyFile);
    if (keyOpt.IsNone()) {
        YRLOG_ERROR("invalid key file path {}", keyFile);
        return nullptr;
    }

    FILE *fp = fopen(keyOpt.Get().c_str(), "r");
    if (!fp) {
        YRLOG_ERROR("unable to open key {}", keyFile);
        return nullptr;
    }

    EVP_PKEY *key =
        PEM_read_PrivateKey(fp, nullptr, nullptr, password.Empty() ? nullptr : const_cast<char *>(password.GetData()));
    if (!key) {
        YRLOG_ERROR("unable to parse key in {}", keyFile);
        fclose(fp);
        return nullptr;
    }
    fclose(fp);
    return key;
}

[[maybe_unused]] static STACK_OF(X509) * GetCAFromFile(const std::string &caFile)
{
    auto caOpt = litebus::os::RealPath(caFile);
    if (caOpt.IsNone()) {
        YRLOG_ERROR("invalid ca file path {}", caFile);
        return nullptr;
    }

    FILE *caCertFile = fopen(caOpt.Get().c_str(), "r");
    if (!caCertFile) {
        YRLOG_ERROR("Failed to open CA certificate file: {}", caFile);
        return nullptr;
    }

    STACK_OF(X509_INFO) *caCertInfoStack = PEM_X509_INFO_read(caCertFile, nullptr, nullptr, nullptr);
    fclose(caCertFile);

    if (!caCertInfoStack) {
        YRLOG_ERROR("Failed to read CA certificate information from file: {}", caFile);
        return nullptr;
    }

    STACK_OF(X509) *caCertStack = sk_X509_new_null();
    for (int i = 0; i < sk_X509_INFO_num(caCertInfoStack); i++) {
        X509_INFO *certInfo = sk_X509_INFO_value(caCertInfoStack, i);
        if (certInfo->x509) {
            sk_X509_push(caCertStack, X509_dup(certInfo->x509));
        }
    }

    sk_X509_INFO_pop_free(caCertInfoStack, X509_INFO_free);

    if (sk_X509_num(caCertStack) == 0) {
        YRLOG_ERROR("No CA certificates found in file: {}", caFile);
        sk_X509_free(caCertStack);
        return nullptr;
    }

    return caCertStack;
}

[[maybe_unused]] static Status GetPemCertsFromFiles(const std::string &certFile, const std::string &keyFile,
                                                    const std::string &caFile,
                                                    const litebus::Option<SensitiveValue> &password, PemCerts &pemCerts)
{
    pemCerts.cert = GetCertFromFile(certFile);
    if (pemCerts.cert == nullptr) {
        ClearPemCerts(pemCerts);
        return Status(FAILED);
    }
    pemCerts.pkey = GetPrivateKeyFromFile(keyFile, password.IsNone() ? SensitiveValue() : password.Get());
    if (pemCerts.pkey == nullptr) {
        ClearPemCerts(pemCerts);
        return Status(FAILED);
    }
    pemCerts.ca = GetCAFromFile(caFile);
    if (pemCerts.ca == nullptr) {
        ClearPemCerts(pemCerts);
        return Status(FAILED);
    }
    return Status::OK();
}

[[maybe_unused]] static SensitiveValue GetSensitivePrivateKeyFromFile(const std::string &keyFile,
                                                                      const SensitiveValue &password)
{
    EVP_PKEY *key = GetPrivateKeyFromFile(keyFile, password);
    if (key == nullptr) {
        return {};
    }
    return GetPrivateKey(key);
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_CERTS_UTILS_H
