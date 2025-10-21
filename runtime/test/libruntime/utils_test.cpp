/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/libruntime/err_type.h"
#include "src/libruntime/fsclient/protobuf/common.pb.h"
#include "src/libruntime/fsclient/protobuf/runtime_service.pb.h"
#include "src/libruntime/utils/datasystem_utils.h"
#include "src/libruntime/utils/security.h"
#include "src/libruntime/utils/utils.h"
#include "src/proto/libruntime.pb.h"

using namespace testing;
using namespace YR::Libruntime;

namespace YR {
namespace test {
class UtilsTest : public ::testing::Test {
public:
    UtilsTest() {}
    ~UtilsTest() {}
};

TEST_F(UtilsTest, SetCallResultWithStackTraceInfoSuccessfully)
{
    CallResult callResult;
    callResult.set_requestid("requestid");
    callResult.set_instanceid("instanceid");

    callResult.set_code(static_cast<common::ErrorCode>(common::ErrorCode::ERR_INNER_COMMUNICATION));
    callResult.set_message("success");

    YR::Libruntime::StackTraceElement element = {"IOException", "testException", "filename", 8};
    std::vector<YR::Libruntime::StackTraceElement> elements = {element};
    YR::Libruntime::StackTraceInfo stackTraceInfo("type", "message", elements, "JAVA");
    std::vector<YR::Libruntime::StackTraceInfo> stackTraces = {stackTraceInfo};

    YR::SetCallResultWithStackTraceInfo(stackTraces, callResult);

    ASSERT_EQ(callResult.stacktraceinfos().size(), 1);
    ASSERT_EQ(callResult.stacktraceinfos()[0].stacktraceelements().size(), 1);
    ASSERT_EQ(callResult.stacktraceinfos()[0].stacktraceelements()[0].classname(), "IOException");
    ASSERT_EQ(callResult.stacktraceinfos()[0].type(), "type");
}

TEST_F(UtilsTest, GetStackTraceInfosSuccessfully)
{
    NotifyRequest notifyRequest;
    notifyRequest.set_requestid("requestid");
    notifyRequest.set_code(static_cast<common::ErrorCode>(common::ErrorCode::ERR_INNER_COMMUNICATION));
    notifyRequest.set_message("success");

    YR::Libruntime::StackTraceElement element = {"IOException", "testException", "filename", 8};
    std::vector<YR::Libruntime::StackTraceElement> elements = {element};
    YR::Libruntime::StackTraceInfo stackTraceInfo("type", "message", elements, "JAVA");
    std::vector<YR::Libruntime::StackTraceInfo> stackTraces = {stackTraceInfo};

    for (size_t i = 0; i < stackTraces.size(); i++) {
        auto setInfo = notifyRequest.add_stacktraceinfos();
        auto eles = stackTraces[i].StackTraceElements();
        for (size_t j = 0; j < eles.size(); j++) {
            auto stackTraceEle = setInfo->add_stacktraceelements();
            stackTraceEle->set_classname(eles[j].className);
            stackTraceEle->set_methodname(eles[j].methodName);
            stackTraceEle->set_filename(eles[j].fileName);
            stackTraceEle->set_linenumber(eles[j].lineNumber);
        }
        setInfo->set_type(stackTraces[i].Type());
        setInfo->set_message(stackTraces[i].Message());
    }

    std::vector<YR::Libruntime::StackTraceInfo> infos = YR::GetStackTraceInfos(notifyRequest);
    ASSERT_EQ(infos.size(), 1);
    ASSERT_EQ(infos[0].StackTraceElements().size(), 1);
    ASSERT_EQ(infos[0].Type(), "type");
}

TEST_F(UtilsTest, SensitiveDataTest)
{
    std::string value = "123";
    SensitiveData sensitiveData;
    SensitiveData sensitiveData2("1234");
    sensitiveData = value.c_str();
    EXPECT_FALSE(sensitiveData == sensitiveData2);

    sensitiveData = sensitiveData2;
    sensitiveData = "";
    sensitiveData2 = "";
    EXPECT_TRUE(sensitiveData == sensitiveData2);

    std::string str = "data";
    std::unique_ptr<char[]> charArray = std::make_unique<char[]>(str.size() + 1);
    std::copy(str.begin(), str.end(), charArray.get());
    charArray[str.size()] = '\0'; 
    SensitiveData d1(std::move(charArray), str.size() + 1);
    SensitiveData d2 = SensitiveData(d1);
    SensitiveData d3 = d1;
    std::unique_ptr<char[]> outData;
    size_t outSize;
    d1.MoveTo(outData, outSize);
    ASSERT_EQ(outSize, 5);
}

TEST_F(UtilsTest, GenerateErrorInfoTest)
{
    auto status = datasystem::Status(datasystem::StatusCode::K_NOT_READY, "ERROR MESSAGE");
    std::vector<std::string> remainIds = {"remainid"};
    std::vector<std::string> ids;
    auto err = GenerateErrorInfo(1, status, 1000, remainIds, ids);
    ASSERT_EQ(err.Code(), ErrorCode::ERR_OK);
    err = GenerateErrorInfo(0, status, 1000, remainIds, ids);
    ASSERT_EQ(err.Code(), ErrorCode::ERR_DATASYSTEM_FAILED);
    status = datasystem::Status(datasystem::StatusCode::K_OUT_OF_MEMORY, "ERROR MESSAGE");
    err = GenerateErrorInfo(0, status, 1000, remainIds, ids);
    ASSERT_EQ(err.Code(), ErrorCode::ERR_SHARED_MEMORY_LIMITED);
    err = GenerateSetErrorInfo(status);
    ASSERT_EQ(err.Code(), ErrorCode::ERR_SHARED_MEMORY_LIMITED);
}

TEST_F(UtilsTest, InitWithDriverTest)
{
    auto security = std::make_shared<Security>();
    auto librtConfig = std::make_shared<LibruntimeConfig>();
    librtConfig->enableMTLS = true;
    librtConfig->verifyFilePath = "test";
    librtConfig->certificateFilePath = "test";
    librtConfig->privateKeyPath = "test";
    librtConfig->serverName = "test";
    librtConfig->encryptEnable = "test";
    librtConfig->encryptEnable = "test";
    librtConfig->runtimePublicKey = "test";
    librtConfig->runtimePrivateKey = "test";
    auto err = security->InitWithDriver(librtConfig);
    ASSERT_EQ(err.Code(), ErrorCode::ERR_OK);
}

}  // namespace test
}  // namespace YR