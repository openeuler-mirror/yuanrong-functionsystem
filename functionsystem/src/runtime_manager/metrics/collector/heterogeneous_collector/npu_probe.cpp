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

#include "npu_probe.h"

#include <unistd.h>

#include <cmath>
#include <utility>
#include <regex>
#include <algorithm>

#include "logs/logging.h"
#include "hex/hex.h"
#include "partitioner.h"
#include "utils/utils.h"


namespace functionsystem::runtime_manager {
const static std::string GET_NPU_TOPO_INFO_CMD = "npu-smi info -t topo";  // NOLINT

const static std::string LS_NPU_DAVINCI_CMD = "ls /dev | grep davinci";  // NOLINT
const static int DEFAULT_HBM_LIMITS = 10000;
const static std::string DEVICE_NUMBER_REGEX = R"(davinci(\d+))";
/* npu-info regex constants for
 * | NPU   Name                | Health        | Power(W)    Temp(C)           Hugepages-Usage(page)|
 * | 0     910B4               | OK            | 85.0        36                0    / 0             |
 * | 0     Ascend910           | OK            | -           35                0    / 0             |
 * */
// \|：匹配竖线 |。
// \s*：匹配任意数量的空白字符
// \s*(\d+)：捕获一个或多个数字 NPU
// \s*(\S+)\s*：捕获一个或多个非空白字符 Name & Health
// \s*(\S+)：匹配 Power(W)
// \s*(\S+)：捕获 Temp(C)
// (\d+)\s*/\s*(\d+): 捕获一个分数形式的数字 Hugepages-Usage(page)
const static std::regex NPU_BASE_INFO_REGEX(
    R"(\|\s*(\d+)\s*(\S+)\s*\|\s*(\S+)\s*\|\s*(\S+)\s*(\S+)\s*(\d+)\s*/\s*(\d+)\s*\|)");

/* npu-info regex constants for
 * | Chip                      | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |
 * | 0                         | 0000:82:00.0  | 82          0    / 0          30759/ 32768         |
 * */
// \s*(\d+)\s*：捕获一个或多个数字 Chip
// \s*(\S+)\s*：捕获一个或多个非空白字符 Bus-Id
// \s*(\d+)\s*：捕获一个带小数点的数字 Power(W)
// (\d+)：捕获一个或多个数字 AICore(%)
// (\d+)\s*/\s*(\d+): 捕获一个分数形式的数字 Memory-Usage(MB)  HBM-Usage(MB)
const static std::regex NPU_CHIP_INFO_REGEX(
    R"(\|\s*(\d+)\s*\|\s*(\S+)\s*\|\s*(\d+)\s*(\d+)\s*/\s*(\d+)\s*(\d+)\s*/\s*(\d+)\s*\|)");

/* npu-info regex constants for (Phy-ID is real device id)
 * | Chip  Phy-ID              | Bus-Id        | AICore(%)   Memory-Usage(MB)  HBM-Usage(MB)        |
 * | 0     10                  | 0000:9D:00.0  | 0           0    / 0          3402 / 65536         |
 * */
// \s*(\d+)\s+：捕获一个或多个数字 Chip
// (\d+)\s*   ：捕获一个或多个数字 Phy-ID
// \s*(\S+)\s*：捕获一个或多个非空白字符 Bus-Id
// \s*(\d+)\s*：捕获一个带小数点的数字 Power(W)
// (\d+)：捕获一个或多个数字 AICore(%)
// (\d+)\s*/\s*(\d+): 捕获一个分数形式的数字 Memory-Usage(MB)  HBM-Usage(MB)
const static std::regex NPU_CHIP_INFO_REGEX_WITH_PHYID(
    R"(\|\s*(\d+)\s+(\d+)\s*\|\s*(\S+)\s*\|\s*(\d+)\s*(\d+)\s*/\s*(\d+)\s*(\d+)\s*/\s*(\d+)\s*\|)");

// Query the basic information about all NPU devices.
const static std::string GET_NPU_BASIC_INFO_CMD = "npu-smi info";  // NOLINT

// Query the device IP information about all NPU devices according to device ID.
const std::string GET_RANK_TABLE_CMD_PREFIX = "hccn_tool -i ";
const std::string GET_RANK_TABLE_CMD_SUFFIX =
    " -ip -g | grep ipaddr: | grep -o [0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*.[0-9][0-9]*";

const int NPU_ID_INDEX = 1;
const int NPU_NAME_INDEX = 2;
const int NPU_HEALTH_INDEX = 3;
const int NPU_PHYSICAL_ID = 2;
const int NPU_USE_MEMORY_INDEX = 4;
const int NPU_TOTAL_MEMORY_INDEX = 5;
const int NPU_USE_HBM_INDEX = 6;
const int NPU_LIMIT_HBM_INDEX = 7;

const int NPU_IP_DEVICE_INDEX = 1;
const int NPU_IP_ADDRESS_INDEX = 2;

const std::string NPU_VDEVICE_CONF_PATH = "/etc/hccn.conf";  // NOLINT

const int INDEX_GAP = 2;
const int PRODUCT_VALUE_INDEX = 2;
const int ROW_INTERVAL = 3;
const int TYPE_INDEX = 4;
const int HBM_INDEX = 7;
const int MEM_INDEX = 6;
const int HBM_VALUE_INDEX = 11;
const int MEM_VALUE_INDEX = 8;

// for ascend-dmi -i
const int ASCEND_DMI_CMD_OUTPUT_LEN = 10;
// length = header + 3 + tail.
const int ASCEND_DMI_COLUMN_SIZE = 5;
const int ASCEND_DMI_STATISTICS_INDEX = 8;
const int ASCEND_DMI_STATISTICS_COUNT_INDEX = 2;
// One card information needs to be displayed in three lines.
const int ASCEND_DMI_ITEM_INDEX_GAP = 3;

NpuProbe::NpuProbe(std::string node, const std::shared_ptr<ProcFSTools> &procFSTools, std::shared_ptr<CmdTool> cmdTool,
                   const std::shared_ptr<XPUCollectorParams> &params)
    : TopoProbe(cmdTool), nodeID(std::move(node))
{
    params_ = params;
    YRLOG_INFO("Init Npu Probe with mode {}", params_->collectMode);
    procFSTools_ = procFSTools;
    npuDeviceInfoPath_ = params->deviceInfoPath;
    InitDevInfo();
    AddLdLibraryPathForNpuCmd(params_->ldLibraryPath);
}

void NpuProbe::InitDevInfo()
{
    npuNum_ = 0;
    devInfo_ = std::make_shared<DevCluster>();
    devInfo_->devType = DEV_TYPE_NPU;
    devInfo_->devVendor = DEV_VENDOR_HUAWEI;
}

size_t NpuProbe::GetLimit() const
{
    return npuNum_;
}

size_t NpuProbe::GetUsage() const
{
    return npuNum_;
}

Status NpuProbe::NPUCollectCount()
{
    return OnGetNPUInfo(true);
}

Status NpuProbe::NPUCollectHBM()
{
    return OnGetNPUInfo(false);
}

Status NpuProbe::NPUCollectSFMD()
{
    auto status = OnGetNPUInfo(false);
    if (status.IsOk()) {
        return GetNPUIPInfo();
    }
    return status;
}

Status NpuProbe::NPUCollectTopo()
{
    auto status = OnGetNPUInfo(false);
    if (status.IsError()) {
        return status;
    }
    return GetNPUTopoInfo();
}

// default mod
Status NpuProbe::NPUCollectAll()
{
    auto status = OnGetNPUInfo(false); // collect count & hbm
    if (status.IsError()) {
        return status;
    }
    status = GetNPUIPInfo(); // collect IP
    if (status.IsError()) {
        return status;
    }
    return GetNPUTopoInfo(); // collect Topo
}

Status NpuProbe::OnGetNPUInfo(bool countMode)
{
    if (countMode) {
        auto status = GetNPUCountInfo();
        if (status.IsOk()) {
            hasXPU_ = true;
            InitHook();
            return status;
        }
    }
    auto status = GetNPUSmiInfo();
    if (status.IsError()) {
        InitDevInfo();
        YRLOG_WARN("There seems to be no npu device on this node. try to get from {}", params_->deviceInfoPath);
        LoadTopoInfo();
        return status;
    }
    hasXPU_ = true;
    InitHook();
    return status;
}

Status NpuProbe::RefreshTopo()
{
    if (init) {
        return Status(StatusCode::SUCCESS);
    }
    init = true;
    auto it = collectFuncMap_.find(params_->collectMode);
    if (it != collectFuncMap_.end()) {
        return (this->*(it->second))();
    }
    YRLOG_WARN("{} is not support", params_->collectMode);
    return Status(StatusCode::FAILED);
}

Status NpuProbe::GetNPUCountInfo()
{
    auto res = cmdTool_->GetCmdResult(LS_NPU_DAVINCI_CMD);
    std::regex deviceNumberRegex(DEVICE_NUMBER_REGEX);
    std::smatch match;
    InitDevInfo();
    std::vector<int> deviceIDs;
    for (const auto &re : res) {
        if (std::regex_search(re, match, deviceNumberRegex)) {
            try {
                devInfo_->devIDs.push_back(std::stoi(match[1]));
            } catch (const std::exception &e) {
                YRLOG_ERROR("parse {} info failed, error is {}", re, e.what());
                npuNum_ = 0;
                return Status{ StatusCode::FAILED, "parse npu count info failed, from /dev" };
            }
            devInfo_->devLimitHBMs.push_back(DEFAULT_HBM_LIMITS);
            devInfo_->devUsedMemory.push_back(0);
            devInfo_->devTotalMemory.push_back(0);
            devInfo_->devUsedHBM.push_back(0);
            devInfo_->health.push_back(0);
            npuNum_ += 1;
            continue;
        }
        YRLOG_DEBUG("parse /dev/{} failed.", re);
    }
    if (npuNum_ == 0) {
        YRLOG_ERROR("can not read dev from /dev");
        return Status{ StatusCode::FAILED, "can not read dev from /dev" };
    }
    devInfo_->devProductModel = "Ascend";  // default name
    std::sort(devInfo_->devIDs.begin(), devInfo_->devIDs.end());
    return Status::OK();
}

Status NpuProbe::ParseNpuSmiInfo(size_t &index, std::string &productModel)
{
    std::smatch match;
    int delta = 0;
    if (std::regex_search(npuSmiCmdOutput_[index], match, NPU_BASE_INFO_REGEX)) {
        if (index + 1 >= npuSmiCmdOutput_.size()) {  // make sure parse npu and chip info in following two line
            YRLOG_ERROR("can not get npu from npu-smi info, no chip info in following line.");
            return Status{ StatusCode::FAILED, "parse npu basic info failed, no chip info in following line." };
        }
        try {
            devInfo_->devIDs.push_back(std::stoi(match[NPU_ID_INDEX]));
            productModel = match[NPU_NAME_INDEX];
            devInfo_->health.push_back(match[NPU_HEALTH_INDEX] == "OK" ? 0 : 1);
        } catch (const std::exception &e) {
            YRLOG_ERROR("parse npu basic info failed, error is {}", e.what());
            return Status{ StatusCode::FAILED, "parse npu basic info failed" };
        }

        index++;  // regex next line
        if (!std::regex_search(npuSmiCmdOutput_[index], match, NPU_CHIP_INFO_REGEX)) {
            if (!std::regex_search(npuSmiCmdOutput_[index], match, NPU_CHIP_INFO_REGEX_WITH_PHYID)) {
                YRLOG_ERROR("parse npu chip info failed.");
                return Status{ StatusCode::FAILED, "parse npu chip info failed." };
            }
            // use physical id as device id
            devInfo_->devIDs[devInfo_->devIDs.size() - 1] = std::stoi(match[NPU_PHYSICAL_ID]);
            delta = 1;
        }
        try {
            devInfo_->devUsedMemory.push_back(std::stoi(match[NPU_USE_MEMORY_INDEX + delta]));
            devInfo_->devTotalMemory.push_back(std::stoi(match[NPU_TOTAL_MEMORY_INDEX + delta]));
            devInfo_->devUsedHBM.push_back(std::stoi(match[NPU_USE_HBM_INDEX + delta]));
            devInfo_->devLimitHBMs.push_back(std::stoi(match[NPU_LIMIT_HBM_INDEX + delta]));
        } catch (const std::exception &e) {
            YRLOG_ERROR("parse npu chip info failed, error is {}", e.what());
            return Status{ StatusCode::FAILED, "parse npu info failed" };
        }
        npuNum_++;  // success parse
    }
    return Status::OK();
}

Status NpuProbe::GetNPUSmiInfo()
{
    npuSmiCmdOutput_ = cmdTool_->GetCmdResult(getNpuStandardInfoCmd_);  // npu-smi info
    if (npuSmiCmdOutput_.empty()) {
        YRLOG_ERROR("can not get npu from npu-smi info, make sure npu-smi is exist!");
        return Status{ StatusCode::FAILED, "can not get npu from npu-smi info, make sure npu-smi is exist!" };
    }
    std::smatch match;
    std::string productModel;
    InitDevInfo();
    for (size_t index = 0; index < npuSmiCmdOutput_.size(); index++) {
        auto status = ParseNpuSmiInfo(index, productModel);
        if (status.IsError()) {
            return status;
        }
    }
    if (npuNum_ == 0) {
        YRLOG_WARN("can not get npu info from npu-smi info");
        return Status{ StatusCode::FAILED, "can not get npu info from npu-smi info" };
    }
    devInfo_->devProductModel = productModel; // only support one type now
    return Status::OK();
}

Status NpuProbe::GetNPUIPInfo()
{
    // here must make sure devInfo_->devIDs.size is equal to npuNum_
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return Status(StatusCode::FAILED, "can not read content, procFSTool is nullptr");
    }
    auto content = procFSTools_->Read(NPU_VDEVICE_CONF_PATH);
    devInfo_->devIPs.clear();
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_WARN("failed to get devs IP from {}, try to get from hccn_tool", NPU_VDEVICE_CONF_PATH);
        return GetDeviceIPsFromHccnTool();
    }
    auto confStr = content.Get();
    std::regex addressRegex(R"(address_(\d+)=(\d+\.\d+\.\d+\.\d+))");
    std::smatch match;
    std::string::const_iterator searchStart(confStr.cbegin());

    std::unordered_map<std::string, std::string> IpMap;
    while (std::regex_search(searchStart, confStr.cend(), match, addressRegex)) {
        IpMap[match[NPU_IP_DEVICE_INDEX].str()] = match[NPU_IP_ADDRESS_INDEX].str();
        searchStart = match.suffix().first;
    }
    if (IpMap.size() < npuNum_) {
        YRLOG_WARN("failed to get ip from {}, npu size({}) is less than NPU num({}), try to get from hccn_tool",
                   NPU_VDEVICE_CONF_PATH, devInfo_->devIPs.size(), npuNum_);
        return GetDeviceIPsFromHccnTool();
    }
    for (auto deviceID : devInfo_->devIDs) {
        if (auto it = IpMap.find(std::to_string(deviceID)); it != IpMap.end()) {
            devInfo_->devIPs.emplace_back(it->second);
        }
    }
    if (devInfo_->devIPs.size() != npuNum_) {
        YRLOG_WARN("failed to get ip from {}, npu size({}) isn't equal to NPU num({})/device size({}), try to get "
            "from hccn_tool",  NPU_VDEVICE_CONF_PATH, devInfo_->devIPs.size(), npuNum_, devInfo_->devIDs.size());
        devInfo_->devIPs.clear();
        return GetDeviceIPsFromHccnTool();
    }
    return Status(StatusCode::SUCCESS);
}

Status NpuProbe::GetNPUTopoInfo()
{
    std::vector<std::string> topoResult = cmdTool_->GetCmdResultWithError(getNpuTopoInfoCmd_);  // npu-smi info -t topo
    if (topoResult.empty() || npuNum_ == 0 || !IsNpuTopoCommandValid(topoResult)) {
        YRLOG_ERROR("please check command: (npu-smi info -t topo) ");
        return Status{ StatusCode::FAILED, "node does not install npu driver" };
    }

    // If you go here, an NPU device must exist.
    devInfo_->devTopo = GetTopoInfo(topoResult, npuNum_);
    // make sure that devInfo_->devTopo is N x N matrix
    bool isCollectMatrix = devInfo_->devTopo.size() != npuNum_;
    for (const auto& topo: devInfo_->devTopo) {
        isCollectMatrix = isCollectMatrix || devInfo_->devTopo.size() != topo.size();
    }
    if (isCollectMatrix) {
        YRLOG_ERROR("failed to get topo info, please check npu-smi info -t topo in command");
        return Status{ StatusCode::FAILED, "failed to get topo info" };
    }
    UpdateTopoPartition();
    return Status::OK();
}

Status NpuProbe::LoadTopoInfo()
{
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return Status(StatusCode::FAILED, "can not read content, procFSTool is nullptr");
    }
    auto content = procFSTools_->Read(npuDeviceInfoPath_);
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_ERROR("failed to read json from {}", npuDeviceInfoPath_);
        return Status(StatusCode::JSON_PARSE_ERROR, "failed to read json from " + npuDeviceInfoPath_);
    }

    auto jsonStr = content.Get();
    nlohmann::json confJson;
    try {
        confJson = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("parse json failed, {}, error: {}", jsonStr, e.what());
        return Status(StatusCode::JSON_PARSE_ERROR, "parse json failed, " + jsonStr + ", error: " + e.what());
    }

    for (const auto &item : confJson.items()) {
        auto nodeName = item.key();
        auto config = item.value();
        if (nodeName.empty()) {
            YRLOG_WARN("empty node name");
            continue;
        }
        BuildTopoConfigMap(config);
    }
    YRLOG_INFO("get npu info form {} successfully.");
    hasXPU_ = true;
    return Status(StatusCode::SUCCESS);
}

Status NpuProbe::BuildTopoConfigMap(const nlohmann::json &config)
{
    if (config.find("nodeName") != config.end()) {
        std::string nodeName = nodeID;
        if (nodeName != config["nodeName"]) {
            YRLOG_WARN("nodeName {} got is not equal to {}", nodeName, config["nodeName"]);
            return Status{ StatusCode::FAILED, "can not find node npu info" };
        }
    }
    if (config.find("number") != config.end()) {
        npuNum_ = config["number"];
    }
    if (config.find("vDeviceIDs") != config.end()) {
        nlohmann::json vDeviceIDs = config.at("vDeviceIDs");
        for (const auto &vDeviceID : vDeviceIDs) {
            devInfo_->devIDs.push_back(vDeviceID);
        }
    }
    if (config.find("vDevicePartition") != config.end()) {
        nlohmann::json vDevicePartition = config.at("vDevicePartition");
        for (const auto &i : vDevicePartition) {
            devInfo_->devPartition.push_back(i);
        }
    }
    if (npuNum_ == 0 || npuNum_ != devInfo_->devIDs.size() || npuNum_ != devInfo_->devPartition.size()) {
        return Status{ StatusCode::FAILED, "failed to parse node npu info from json." };
    }
    return Status::OK();
}

void NpuProbe::UpdateHealth()
{
    if (params_->collectMode == NPU_COLLECT_COUNT) {
        return;
    }
    npuSmiCmdOutput_ = cmdTool_->GetCmdResult(getNpuStandardInfoCmd_);  // npu-smi info
    if (npuSmiCmdOutput_.empty()) {
        YRLOG_ERROR("can not get npu from npu-smi info, failed to update NPU health!");
        return;
    }
    std::smatch match;
    std::vector<int> newHealth;
    for (size_t index = 0; index < npuSmiCmdOutput_.size(); index++) {
        if (std::regex_search(npuSmiCmdOutput_[index], match, NPU_BASE_INFO_REGEX)) {
            try {
                newHealth.push_back(match[NPU_HEALTH_INDEX] == "OK" ? 0 : 1);
            } catch (const std::exception &e) {
                YRLOG_ERROR("parse npu basic info failed, error is {}", e.what());
                newHealth.push_back(0);
            }
            index++;
        }
    }
    if (newHealth.size() != npuNum_) {
        YRLOG_ERROR(
            "parse npu basic info failed, failed to update NPU health because npuNum is not equal to health size");
        return;
    }
    devInfo_->health.clear();
    devInfo_->health = newHealth;
}

void NpuProbe::UpdateTopoPartition()
{
    // note: The function of collecting topology information is unclear.
    // And Topo information is not used for scheduling. Just Keep it.
    if (devInfo_->devTopo.empty()) {
        return;
    }

    auto totalSlots = pow(2, ceil(log2((double(devInfo_->devTopo.size())))));
    devInfo_->devPartition.resize(size_t(totalSlots));
    Partitioner partitioner = Partitioner();
    auto partitionInfo = partitioner.GetPartition(ConvertPartition(devInfo_->devTopo));
    size_t index = 0;
    for (auto partition : partitionInfo) {
        auto idIndex = static_cast<unsigned short>(partition);
        if (partition != -1 && idIndex < devInfo_->devIDs.size()) {
            devInfo_->devPartition[index] = std::to_string(devInfo_->devIDs[idIndex]);
        } else {
            devInfo_->devPartition[index] = "null";
        }
        index++;
    }
}

void NpuProbe::UpdateDevTopo()
{
    std::vector<std::string> topoResult = cmdTool_->GetCmdResult(getNpuTopoInfoCmd_);
    if (topoResult.empty() || !IsNpuTopoCommandValid(topoResult)) {
        YRLOG_ERROR("The node does not install npu driver");
        return;
    }

    // If you go here, an NPU device must exist.
    devInfo_->devTopo = GetTopoInfo(topoResult, npuNum_);
    UpdateTopoPartition();
}

void NpuProbe::UpdateHBM()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateMemory()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateUsedMemory()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateUsedHBM()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateProductModel()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateDeviceIDs()
{
    GetNPUSmiInfo();
}

void NpuProbe::UpdateDeviceIPs()
{
    GetNPUIPInfo();
}

Status NpuProbe::GetDeviceIPsFromHccnTool()
{
    bool isSuccess = true;
    if (devInfo_->devIDs.size() != npuNum_) {
        YRLOG_ERROR("get ip failed because device ids size is not equal to npu number");
        return Status{ StatusCode::FAILED, "device ids size is not equal to npu number" };
    }
    for (size_t i = 0; i < npuNum_; i++) {
        auto devID = devInfo_->devIDs[i];
        std::string getRankTableCmd = getNpuIPInfoCmd_ + std::to_string(devID) + GET_RANK_TABLE_CMD_SUFFIX;
        std::vector<std::string> ipaddr = cmdTool_->GetCmdResult(getRankTableCmd);  // hccn_tool -i
        if (ipaddr.empty()) {
            YRLOG_ERROR("failed to get dev({}) IP with cmd: {}", devID, getRankTableCmd);
            devInfo_->devIPs.emplace_back("");
            isSuccess = false;
        } else {
            devInfo_->devIPs.emplace_back(litebus::strings::Trim(ipaddr[0])); // trim \n
        }
    }
    if (isSuccess) {
        return Status::OK();
    }
    return Status{ StatusCode::FAILED, "failed to get all ip with hccn_tool" };
}

bool NpuProbe::IsNpuTopoCommandValid(std::vector<std::string> lines)
{
    if (lines.empty()) {
        return false;
    }
    std::stringstream ss;
    for (const auto &line : lines) {
        ss << line << "\n";
    }
    auto output = ss.str();
    YRLOG_DEBUG(output);

    // Check if the output contains the error message indicating the command is invalid.
    static const std::string KEY_MSG1 = "NPU";
    static const std::string NOT_SUPPORT_MSG = "not support";
    static const std::string INVALID_MSG = "invalid";
    return (output.find(KEY_MSG1) != std::string::npos) && output.find(NOT_SUPPORT_MSG) == std::string::npos
           && output.find(INVALID_MSG) == std::string::npos;
}

void NpuProbe::AddLdLibraryPathForNpuCmd(const std::string &ldLibraryPath)
{
    // we don't support ascend-dmi
    getNpuTopoInfoCmd_ = Utils::LinkCommandWithLdLibraryPath(ldLibraryPath, GET_NPU_TOPO_INFO_CMD);
    getNpuStandardInfoCmd_ = Utils::LinkCommandWithLdLibraryPath(ldLibraryPath, GET_NPU_BASIC_INFO_CMD);
    getNpuIPInfoCmd_ = Utils::LinkCommandWithLdLibraryPath(ldLibraryPath, GET_RANK_TABLE_CMD_PREFIX);
}

void NpuProbe::InitHook()
{
}
}  // namespace functionsystem::runtime_manager