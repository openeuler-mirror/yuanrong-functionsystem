#include <iostream>

#include <gmock/gmock.h>

#include "async/result.hpp"

using litebus::Result;

TEST(ResultTest, ConstructInit)
{
    Result<int> result;
    EXPECT_TRUE(result.GetStatus().IsInit());

    EXPECT_FALSE(result.IsError());
    EXPECT_FALSE(result.IsOK());

    EXPECT_TRUE(result.IsNone<0>());
    EXPECT_FALSE(result.IsSome<0>());
}

TEST(ResultTest, Construct)
{
    bool a = true;
    int b = 100;
    float c = 1.1111;

    Result<bool, int, float> result(a, b, c, litebus::Status::KOK);
    EXPECT_TRUE(result.IsOK());
    EXPECT_FALSE(result.IsError());

    EXPECT_EQ(a, result.Get<0>().Get());
    EXPECT_EQ(b, result.Get<1>().Get());
    EXPECT_EQ(c, result.Get<2>().Get());
}

TEST(ResultTest, SetStatus)
{
    bool a = true;
    int b = 100;
    float c = 1.1111;

    Result<bool, int, float> result(a, b, c, litebus::Status::KOK);
    EXPECT_TRUE(result.IsOK());
    EXPECT_FALSE(result.IsError());

    litebus::Status::Code code = 100;
    result.SetStatus(code);
    EXPECT_FALSE(result.IsOK());
    EXPECT_TRUE(result.IsError());

    EXPECT_EQ(a, result.Get<0>().Get());
    EXPECT_EQ(b, result.Get<1>().Get());
    EXPECT_EQ(c, result.Get<2>().Get());
}
