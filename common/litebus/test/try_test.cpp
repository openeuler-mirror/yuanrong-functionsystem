#include <gtest/gtest.h>

#include "async/common.hpp"
#include "async/failure.hpp"
#include "async/option.hpp"
#include "async/try.hpp"

using litebus::Failure;
using litebus::Nothing;
using litebus::Option;
using litebus::Try;

TEST(TryTests, Compare)
{
    Try<int> one = 1;
    EXPECT_FALSE(one.IsError());
    EXPECT_EQ(1, one.Get());

    Try<Nothing> xx = Failure(-1);
    EXPECT_TRUE(xx.IsError());
    EXPECT_EQ(-1, xx.GetErrorCode());
}
