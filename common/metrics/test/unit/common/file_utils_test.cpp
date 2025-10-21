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
#include <fstream>
#include <gtest/gtest.h>
#include <utils/os_utils.hpp>

#include "common/file/file_utils.h"

namespace observability::test {
using namespace observability::metrics::common;

TEST(CommonFileUtilTest, UtilSimpleTest)
{
    std::string filepath_not_exist = "/tmp/filepath_not_exist";
    int64_t timestamp = 0;
    GetFileModifiedTime(filepath_not_exist, timestamp);
    EXPECT_EQ(timestamp, 0);

    std::vector<std::string> files;
    observability::metrics::common::Glob(filepath_not_exist, files);
    int compress = observability::metrics::common::CompressFile(filepath_not_exist, "dest");
    EXPECT_EQ(compress, -1);

    std::ofstream outfile;
    std::string filepath = "/tmp/temp.log";
    outfile.open(filepath.c_str());
    outfile << "1";
    outfile.close();

    auto compress_ano = observability::metrics::common::CompressFile(filepath, "/tmp/");
    EXPECT_EQ(compress_ano, -1);

    observability::metrics::common::DeleteFile("/tmp/");
    litebus::os::Rm(filepath);
}

TEST(CommonFileUtilTest, UtilCompressTest)
{
    std::string filepath_not_exist = "/tmp/metrics/filepath_not_exist";
    int compress = observability::metrics::common::CompressFile(filepath_not_exist, "dest");
    EXPECT_EQ(compress, -1);

    std::ofstream MyFile("/tmp/metrics/compress_util.txt");
    MyFile << "util compress";
    MyFile.close();

    int compress_exist = observability::metrics::common::CompressFile("/tmp/metrics/compress_util.txt",
                                                        "/tmp/metrics/compress_util.tar.gz");
    EXPECT_EQ(compress_exist, 0);

    int64_t timestamp;
    observability::metrics::common::GetFileModifiedTime("/tmp/metrics/compress_util.tar.gz", timestamp);
    ASSERT_NE(timestamp, 0);
    std::remove("/tmp/metrics/compress_util.txt");
    std::remove("/tmp/metrics/compress_util.tar.gz");
}
}