/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_CONFIG_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_CONFIG_H

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "common/logs/logging.h"

namespace function_master {

struct TenantQuota {
    int64_t cpuMillicores{ 0 };   // CPU 毫核
    int64_t memLimitMb{ 0 };      // 内存 MB
    int64_t cooldownMs{ 10000 };  // 冷却时间，默认 10s
};

class QuotaConfig {
public:
    virtual ~QuotaConfig() = default;

    // Flag 未配置（path 为空）：关闭限额控制，打印 WARNING
    // Flag 已配置但文件不存在或解析失败：YRLOG_FATAL + 终止
    static QuotaConfig LoadFromFile(const std::string &path);

    // 查询顺序：perTenantQuota_[tenantID] → defaultQuota_
    TenantQuota GetQuota(const std::string &tenantID) const;

    bool IsEnabled() const;

    // 预留：外部接口写入 per-tenant quota（首阶段不调用）
    void UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota);

private:
    bool enabled_{ true };
    TenantQuota defaultQuota_;
    std::unordered_map<std::string, TenantQuota> perTenantQuota_;
};

inline QuotaConfig QuotaConfig::LoadFromFile(const std::string &path)
{
    QuotaConfig cfg;
    // 内置兜底默认值
    cfg.defaultQuota_ = TenantQuota{ 32000, 65536, 10000 };

    if (path.empty()) {
        cfg.enabled_ = false;
        YRLOG_WARN("quota_config_file not set, quota control disabled");
        return cfg;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        YRLOG_FATAL("quota_config_file not found: {}", path);
        std::terminate();
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception &e) {
        YRLOG_FATAL("quota_config_file parse error: {}, path: {}", e.what(), path);
        std::terminate();
    }

    auto parseQuota = [](const nlohmann::json &node) -> TenantQuota {
        TenantQuota q;
        q.cpuMillicores = node.value("cpuMillicores", int64_t{ 32000 });
        q.memLimitMb    = node.value("memMb",          int64_t{ 65536 });
        q.cooldownMs    = node.value("cooldownMs",     int64_t{ 10000 });
        return q;
    };

    if (j.contains("default")) {
        cfg.defaultQuota_ = parseQuota(j["default"]);
    }

    return cfg;
}

inline TenantQuota QuotaConfig::GetQuota(const std::string &tenantID) const
{
    auto it = perTenantQuota_.find(tenantID);
    if (it != perTenantQuota_.end()) {
        return it->second;
    }
    return defaultQuota_;
}

inline bool QuotaConfig::IsEnabled() const
{
    return enabled_;
}

inline void QuotaConfig::UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota)
{
    enabled_ = true;
    perTenantQuota_[tenantID] = quota;
}

}  // namespace function_master

#endif
