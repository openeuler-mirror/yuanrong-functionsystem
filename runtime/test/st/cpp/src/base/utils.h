/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef TEST_ST_CASES_SRC_BASE_UTILS_H
#define TEST_ST_CASES_SRC_BASE_UTILS_H
#include <string>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "yr/yr.h"

static void ErrorMsgCheck(std::string error_code, std::string error_msg, std::string excep_msg)
{
    std::string::size_type code_idx = excep_msg.find(error_code);
    std::string::size_type msg_idx = excep_msg.find(error_msg);
    ASSERT_TRUE(code_idx != std::string::npos);
    ASSERT_TRUE(msg_idx != std::string::npos);
}

#define EXPECT_THROW_WITH_CODE_AND_MSG(statement, code, msg) \
    EXPECT_THROW({                                           \
        try {                                                \
            (statement);                                     \
        } catch (const YR::Exception &e) {                   \
            EXPECT_EQ(e.Code(), (code));                     \
            EXPECT_THAT(e.Msg(), testing::HasSubstr(msg));   \
            throw;                                           \
        }                                                    \
    }, YR::Exception)

#endif  // TEST_ST_CASES_SRC_BASE_UTILS_H
