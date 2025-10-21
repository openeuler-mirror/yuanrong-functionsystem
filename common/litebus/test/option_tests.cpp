#include <gtest/gtest.h>

#include "async/option.hpp"

using litebus::Option;
using std::string;

namespace litebus {

TEST(OptionTests, compare)
{
    Option<int> none = None();
    EXPECT_FALSE(none == 1);
    EXPECT_TRUE(none != 1);    // call operator!=(const T& t)

    Option<int> one = 1;
    EXPECT_TRUE(one.IsSome());
    EXPECT_EQ(1, one.Get());
    EXPECT_EQ(one, 1);       // call operator==(const T& t)
    EXPECT_NE(none, one);    // call operator!=(Const Option<T>& t)
    Option<int> _one = Some(1);
    EXPECT_EQ(one, _one);    // call operator==(const Option<T>& t)

    Option<int> copied_one(one);
    EXPECT_EQ(one, copied_one);

    Option<string> str = string("hello");
    EXPECT_EQ("hello", str.Get());
}

TEST(OptionTests, ChangSome)
{
    Option<string> str = None();
    str = string("connect");
    EXPECT_EQ(true, str.IsSome());
}

TEST(OptionTests, NoneChangSomeNone)
{
    Option<string> str = None();
    str = string("connect");
    str = Option<string>(None());
    EXPECT_EQ(true, str.IsNone());
}

TEST(OptionTests, SomeChangNone)
{
    Option<string> str = string("connect");
    str = Option<string>(None());
    EXPECT_EQ(true, str.IsNone());
}

TEST(OptionTests, NoneMiltyChang1)
{
    Option<string> str = None();
    str = string("connect");
    str = Option<string>(None());
    str = string("conn");
    EXPECT_EQ(true, str.IsSome());
}

TEST(OptionTests, NoneMiltyChang2)
{
    Option<string> str = string("conn1");
    str = Option<string>(None());
    str = Option<string>(None());
    str = string("conn2");
    str = string("conn3");
    str = None();
    EXPECT_EQ(true, str.IsNone());
}

class TestOption {
public:
    TestOption()
    {
        BUSLOG_INFO("TestOption(): s1={}", s1);
    }

    TestOption(const TestOption &that) : s1(that.s1)
    {
        BUSLOG_INFO("TestOption(const TestOption& that): s1={}", s1);
    }
    TestOption(TestOption &&that) : s1(std::move(that.s1))
    {
        BUSLOG_INFO("TestOption(TestOption&& that): s1={}", s1);
    }
    TestOption(const std::string &s) : s1(s)
    {
        BUSLOG_INFO("TestOption(const std::string &s): s1={}", s1);
    }

    ~TestOption()
    {
        BUSLOG_INFO("~TestOption(): s1={}", s1);
    }

    TestOption &operator=(const TestOption &that)
    {
        if (this == &that) {
            return *this;
        }
        s1 = that.s1;
        return *this;
    }

    std::string s1;
};

TEST(OptionTests, TestOptions)
{
    BUSLOG_INFO("-----------Option<TestOption> obj = None()");
    Option<TestOption> obj = None();

    EXPECT_EQ(true, obj.IsNone());

    BUSLOG_INFO("--------------Option<TestOption> obj1 = TestOption(test1);");
    Option<TestOption> obj1 = TestOption("test1");
    EXPECT_EQ(true, obj1.IsSome());

    BUSLOG_INFO("-----------obj = obj1;");
    obj = obj1;
    EXPECT_EQ(true, obj.IsSome());

    BUSLOG_INFO("---------------Option<TestOption> obj3");
    Option<TestOption> obj3;
    BUSLOG_INFO("--------------- obj3 = obj1");
    obj3 = obj1;

    EXPECT_EQ(true, obj3.IsSome());

    BUSLOG_INFO("-------------- Option<TestOption> obj4 = std::move(obj3);");
    Option<TestOption> obj4 = std::move(obj3);

    BUSLOG_INFO("--------------");
    EXPECT_EQ(true, obj4.IsSome());
}

}    // namespace litebus
