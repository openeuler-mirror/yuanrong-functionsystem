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

#include <sys/stat.h>
#include <unistd.h>
#include <set>

#include "common/utils/files.h"
#include "disk_collector.h"

namespace functionsystem::runtime_manager {

const std::regex DEFAULT_PATH_REGEX_PATTERN(R"(^\/[a-zA-Z0-9_\-\/\.]+/$)");
const int MAX_PATH_LEN = 8192;

bool ValidatePathSecurity(const std::string &path)
{
    // [Path Length Check] Ensure the path length does not exceed 8096 characters
    if (path.size() > MAX_PATH_LEN) {
        YRLOG_WARN("Invalid path len: {}", path.size());
        return false;
    }

    // [Format::Path Check] Path must:
    //  - Start with '/' and end with '/'
    //  - Contain only allowed characters: [a-zA-Z0-9_\.\/]
    if (!std::regex_match(path, DEFAULT_PATH_REGEX_PATTERN)) {
        YRLOG_WARN("Invalid path format: {}", path);
        return false;
    }

    // [Traversal Check] Block paths containing ".." to prevent directory traversal
    if (path.find("..") != std::string::npos) {
        YRLOG_WARN("Path traversal detected: {}", path);
        return false;
    }

    return true;
}

// Validates size format (e.g., "5G", "10G")
bool ValidateDiskSizeFormat(const std::string& size)
{
    // [Format::Size Check] Require: [digits] + [G]
    const std::regex sizeRegex(R"(^\d+G$)");
    return std::regex_match(size, sizeRegex);
}

// Extracts numeric part from size string (e.g., "40G" �� 40).
bool ExtractNumberFromDiskSize(const std::string &sizeStr, int &size)
{
    std::regex numRegex(R"(\d+)");
    std::smatch match;
    if (std::regex_search(sizeStr, match, numRegex)) {
        try {
            size = std::stoi(match.str());
            return true;
        } catch (const std::invalid_argument&) {
            YRLOG_WARN("Invalid number format in string: {}", sizeStr);
            return false;
        } catch (const std::out_of_range&) {
            YRLOG_WARN("Number out of range in string: {}", sizeStr);
            return false;
        }
    }
    return false;
}

bool DiskCollector::ValidateDiskConfig(const nlohmann::json& disk)
{
    // 1. Check mandatory fields
    if (!disk.contains("name") || !disk.contains("size") || !disk.contains("mountPoints")) {
        YRLOG_WARN("Missing required fields in: {}", disk.dump());
        return false;
    }

    // 2. Validate disk size format
    std::string size = disk["size"].get<std::string>();
    if (!ValidateDiskSizeFormat(size)) {
        YRLOG_WARN("Invalid disk size format: {}", size);
        return false;
    }

    // 3. Validate mount path security
    std::string mountPath = disk["mountPoints"].get<std::string>();
    if (!ValidatePathSecurity(mountPath)) {
        YRLOG_WARN("Mount path validation failed: {}", mountPath);
        return false;
    }

    return true;
}

void DiskCollector::ParseDiskConfig(const nlohmann::json& disk)
{
    resource_view::ResourceExtension diskContent;

    std::string sizeStr = disk["size"].get<std::string>();
    int size;

    if (!ExtractNumberFromDiskSize(sizeStr, size)) {
        YRLOG_WARN("Invalid disk size format: {}", sizeStr);
        return;
    }
    diskSizes_.push_back(size);
    diskContent.mutable_disk()->set_size(size);
    diskContent.mutable_disk()->set_name(disk["name"].get<std::string>());
    diskContent.mutable_disk()->set_mountpoints(disk["mountPoints"].get<std::string>());
    diskContents_.push_back(diskContent);
}

std::string DiskCollector::GenFilter() const
{
    // system-disk
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

bool DiskCollector::InitFromConfig(const std::string& diskConfig)
{
    nlohmann::json disks;
    try {
        disks = nlohmann::json::parse(diskConfig);
    } catch (nlohmann::json::parse_error &e) {
        YRLOG_ERROR("JSON parse failed: {}", e.what());
        return false;
    }

    for (auto &disk : disks) {
        try {
            if (!ValidateDiskConfig(disk)) {
                YRLOG_WARN("Invalid disk config: {}", disk.dump());
                continue;
            }
            ParseDiskConfig(disk);
        } catch (std::exception &e) {
            YRLOG_WARN("Disk config processing error: {}", e.what());
            continue;
        }
    }
    return true;
}

DiskCollector::DiskCollector(const std::string &diskConfig)
    : BaseMetricsCollector(metrics_type::DISK, collector_type::SYSTEM, nullptr)
{
    uuid_ = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    if (!InitFromConfig(diskConfig)) {
        YRLOG_ERROR("DiskCollector initialization failed");
    }
}

litebus::Future<Metric> DiskCollector::GetUsage() const
{
    Metric metric;
    if (diskSizes_.size() == 0) {
        return metric;
    }

    DevClusterMetrics devClusterMetrics;
    devClusterMetrics.uuid = uuid_;
    devClusterMetrics.intsInfo[resource_view::DISK_RESOURCE_NAME] = diskSizes_;
    devClusterMetrics.extensionInfo.insert(devClusterMetrics.extensionInfo.end(),
                                           diskContents_.begin(), diskContents_.end());
    metric.devClusterMetrics = std::move(devClusterMetrics);
    return metric;
}

Metric DiskCollector::GetLimit() const
{
    Metric metric;
    DevClusterMetrics devClusterMetrics;
    devClusterMetrics.extensionInfo.insert(devClusterMetrics.extensionInfo.end(),
                                           diskContents_.begin(), diskContents_.end());
    return metric;
}

}  // namespace functionsystem::runtime_manager