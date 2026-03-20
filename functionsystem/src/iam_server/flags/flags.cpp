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

#include "flags.h"

#include "common/constants/constants.h"
#include "common/utils/param_check.h"

namespace functionsystem::iamserver {
namespace {
const uint32_t DEFAULT_TOKEN_EXPIRED_TIME_SPAN = 24 * 60 * 60;  // 24 hours, unit: s
const uint32_t MIN_TOKEN_EXPIRED_TIME_SPAN = 12 * 60;           // unit: s
const uint32_t MAX_TOKEN_EXPIRED_TIME_SPAN = 7 * 24 * 60 * 60;  // unit: s
}  // namespace
using namespace litebus::flag;
Flags::Flags()
{
    AddFlag(&Flags::logConfig, "log_config", "json format string. For log initialization.", "");
    AddFlag(&Flags::nodeID, "node_id", "vm id", "");
    AddFlag(&Flags::ip, "ip", "IP address for listening.", true, FlagCheckWrraper(IsIPValid));
    AddFlag(&Flags::httpListenPort, "http_listen_port", "For posix server listening. example: 8080", true,
            FlagCheckWrraper(IsPortValid));
    AddFlag(&Flags::metaStoreAddress, "meta_store_address", "For MetaStorage service discover", "");
    AddFlag(&Flags::enableTrace, "enable_trace", "For trace enable, example: false", false);
    AddFlag(&Flags::enableIAM_, "enable_iam", "enable verify and authorize token of internal request", false);
    // tokenExpiredTimeSpan = 0 means token never expires
    AddFlag(&Flags::tokenExpiredTimeSpan_, "token_expired_time_span",
            "token alive period of internal request, 0 means never expire", DEFAULT_TOKEN_EXPIRED_TIME_SPAN,
            NumCheck(0u, MAX_TOKEN_EXPIRED_TIME_SPAN));
    AddFlag(&Flags::decryptAlgorithm_, "decrypt_algorithm", "decrypt algorithm", std::string("NO_CRYPTO"),
            WhiteListCheck({ "NO_CRYPTO" }));
    AddFlag(&Flags::resourcePath_, "resource_path", "resource path to read secret key files", "/");
    AddFlag(&Flags::k8sBasePath_, "k8s_base_path", "For k8s service discovery.", "");
    AddFlag(&Flags::k8sNamespace_, "k8s_namespace", "k8s cluster namespace", "default");
    AddFlag(&Flags::electionMode_, "election_mode", "selection mode, eg: standalone,etcd,txn,k8s",
            std::string("standalone"), WhiteListCheck({ "etcd", "txn", "k8s", "standalone" }));
    AddFlag(&Flags::electLeaseTTL_, "elect_lease_ttl", "lease ttl of function master election", DEFAULT_ELECT_LEASE_TTL,
            NumCheck(MIN_ELECT_LEASE_TTL, MAX_ELECT_LEASE_TTL));
    AddFlag(&Flags::electKeepAliveInterval_, "elect_keep_alive_interval", "interval of elect's lease keep alive",
            DEFAULT_ELECT_KEEP_ALIVE_INTERVAL, NumCheck(MIN_ELECT_KEEP_ALIVE_INTERVAL, MAX_ELECT_KEEP_ALIVE_INTERVAL));
    AddFlag(&Flags::iamCredentialType_, "iam_credential_type", "credential type for iam", IAM_CREDENTIAL_TYPE_TOKEN,
            WhiteListCheck({ IAM_CREDENTIAL_TYPE_TOKEN, IAM_CREDENTIAL_TYPE_AK_SK }));
    AddFlag(&Flags::permanentCredentialConfigPath_, "permanent_cred_conf_path", "permanent credential config path",
            "/home/sn/config/permanent-credential-config.json");
    AddFlag(&Flags::credentialHostAddress_, "credential_host_address", "credential host platform address", "");
    AddFlag(&Flags::keycloakUrl_, "keycloak_url", "Keycloak server URL", "");
    AddFlag(&Flags::keycloakPublicUrl_, "keycloak_public_url", "Keycloak public URL for browser-side redirect URLs",
            "");
    AddFlag(&Flags::keycloakClientId_, "keycloak_client_id", "Keycloak client ID for frontend", "");
    AddFlag(&Flags::keycloakClientSecret_, "keycloak_client_secret", "Keycloak client secret for frontend", "");
    AddFlag(&Flags::keycloakIssuerUrl_, "keycloak_issuer_url",
            "Keycloak issuer URL for JWT iss validation (defaults to keycloak_url)", "");
    AddFlag(&Flags::keycloakRealm_, "keycloak_realm", "Keycloak realm name", "");
    AddFlag(&Flags::keycloakEnabled_, "keycloak_enabled", "Enable Keycloak token exchange", false);
    AddFlag(&Flags::keycloakCacheTtlSeconds_, "keycloak_cache_ttl_seconds", "JWKS cache TTL in seconds", 300);

    AddFlag(&Flags::authProvider_, "auth_provider", "External auth provider: keycloak or casdoor",
            std::string("casdoor"));
    AddFlag(&Flags::casdoorEnabled_, "casdoor_enabled", "Enable Casdoor integration", false);
    AddFlag(&Flags::casdoorEndpoint_, "casdoor_endpoint", "Casdoor internal endpoint", "");
    AddFlag(&Flags::casdoorPublicEndpoint_, "casdoor_public_endpoint", "Casdoor public endpoint", "");
    AddFlag(&Flags::casdoorClientId_, "casdoor_client_id", "Casdoor Client ID", "");
    AddFlag(&Flags::casdoorClientSecret_, "casdoor_client_secret", "Casdoor Client Secret", "");
    AddFlag(&Flags::casdoorOrganization_, "casdoor_organization", "Casdoor Organization", "");
    AddFlag(&Flags::casdoorApplication_, "casdoor_application", "Casdoor Application", "");
    AddFlag(&Flags::casdoorAdminUser_, "casdoor_admin_user", "Casdoor admin username", "");
    AddFlag(&Flags::casdoorAdminPassword_, "casdoor_admin_password", "Casdoor admin password", "");
    AddFlag(&Flags::casdoorJwtPublicKey_, "casdoor_jwt_public_key", "Casdoor JWT Public Key (PEM)", "");
}

Flags::~Flags()
{
}
}  // namespace functionsystem::iamserver
