/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "function_agent/snapshot/snapshot_storage_config.h"

#include <utility>

#include "common/crypto/crypto.h"

namespace functionsystem::function_agent {
namespace {

Status DecryptCredential(const std::string &fieldName, const std::string &ciphertext,
                         const SnapshotCredentialDecryptor &decryptor, std::string &output)
{
    if (ciphertext.empty()) {
        return Status::OK();
    }
    auto plaintext = decryptor(ciphertext);
    if (plaintext.IsNone()) {
        return Status(StatusCode::PARAMETER_ERROR, "failed to decrypt " + fieldName);
    }
    output = plaintext.Get().GetData();
    return Status::OK();
}

}  // namespace

Status BuildSnapshotStorageStartConfig(const FunctionAgentFlags &flags,
                                       SnapshotStorageStartConfig &output,
                                       const SnapshotCredentialDecryptor &decrypt)
{
    output = {};
    if (!flags.GetEnableSandboxPauseResume()) {
        return Status::OK();
    }

    SnapshotStorageStartConfig candidate;
    candidate.enabled = true;
    candidate.backend = flags.GetSnapshotStorageBackend();
    if (candidate.backend == "datasystem") {
        candidate.dataSystem.host = flags.GetDataSystemHost();
        candidate.dataSystem.port = flags.GetDataSystemPort();
        output = std::move(candidate);
        return Status::OK();
    }
    if (candidate.backend != "obs") {
        output = std::move(candidate);
        return Status::OK();
    }

    candidate.obs.endpoint = flags.GetSnapshotObsEndpoint();
    candidate.obs.bucket = flags.GetSnapshotObsBucket();
    candidate.obs.useHttps = flags.GetSnapshotObsUseHttps();
    candidate.obs.pathStyle = flags.GetSnapshotObsPathStyle();
    SnapshotCredentialDecryptor decryptor = decrypt;
    if (!decryptor) {
        decryptor = [](const std::string &ciphertext) {
            return Crypto::GetInstance().Decrypt(ciphertext);
        };
    }
    RETURN_IF_NOT_OK(DecryptCredential("snapshot_obs_access_key", flags.GetSnapshotObsAccessKey(), decryptor,
                                      candidate.obs.accessKey));
    RETURN_IF_NOT_OK(DecryptCredential("snapshot_obs_secret_key", flags.GetSnapshotObsSecretKey(), decryptor,
                                      candidate.obs.secretKey));
    RETURN_IF_NOT_OK(DecryptCredential("snapshot_obs_security_token", flags.GetSnapshotObsSecurityToken(), decryptor,
                                      candidate.obs.securityToken));
    output = std::move(candidate);
    return Status::OK();
}

}  // namespace functionsystem::function_agent
