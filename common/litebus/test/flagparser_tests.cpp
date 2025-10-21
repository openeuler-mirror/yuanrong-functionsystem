/*
 * flagparser_tests.cpp
 *
 *  Created on: 2018-11-17
 *      Author:
 */

#include <iostream>
#include <map>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "async/flag_parser_impl.hpp"
#include "async/option.hpp"

using litebus::Option;
using litebus::Some;
using litebus::flag::FlagParser;
using std::map;
using std::string;

#define ARRAY_SIZE(arr) ((sizeof(arr)) / (sizeof(arr[0])))

class TestFlagParser : public virtual FlagParser {
public:
    TestFlagParser()
    {
        AddFlag(&TestFlagParser::field1, "field1", "Set field1");

        AddFlag(&TestFlagParser::field2, "field2", "Set field2", 42);

        AddFlag(&TestFlagParser::field3, "field3", "Set field3", false);

        AddFlag(&TestFlagParser::field4, "field4", "Set field4");

        AddFlag(&TestFlagParser::field5, "field5", "Set field5");

        AddFlag(&TestFlagParser::field9, "field9", "Set field9", true, litebus::flag::NumCheck(0, 100));

        AddFlag(&TestFlagParser::field6, "field6", "Set field6", "xxxx");

        AddFlag(&TestFlagParser::field7, "field7", "Set field7", 42, litebus::flag::NumCheck(0, 100));

        AddFlag(&TestFlagParser::field8, "field8", "Set field8", "/", litebus::flag::RealPath());
    }

    string field1;
    int field2;
    bool field3;
    Option<bool> field4;
    Option<bool> field5;
    string field6;
    int field7;
    string field8;
    int field9;
};

TEST(FlagsTest, ParseFlags)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test",   "--field1=hello field1", "--field2=50",
                           "--field3=false", "--field4=false",        "--field5=true", "--field9=50" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_TRUE(ret.IsNone());
    EXPECT_EQ("hello field1", flags.field1);
    EXPECT_EQ(50, flags.field2);
    EXPECT_FALSE(flags.field3);
    EXPECT_TRUE(flags.field4.IsSome());
    EXPECT_FALSE(flags.field4.Get());
    EXPECT_TRUE(flags.field5.IsSome());
    EXPECT_TRUE(flags.field5.Get());
    EXPECT_EQ(50, flags.field9);
    EXPECT_EQ("xxxx", flags.field6);
}

TEST(FlagsTest, RequiredFlagNotProvided)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field2=50", "--field3=false", "--field4=false", "--field5=true" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_FALSE(ret.IsNone());
}

TEST(FlagsTest, EmptryString)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field1=hello field1", "--field2=50", "--field3=false", "--field4=false",
                           "--field5=true",
                           "--field6=",
                           "--field8=/usr/",
                           "--field9=50"
                           "" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_TRUE(ret.IsNone());
    EXPECT_EQ("hello field1", flags.field1);
    EXPECT_EQ(50, flags.field2);
    EXPECT_FALSE(flags.field3);
    EXPECT_TRUE(flags.field4.IsSome());
    EXPECT_FALSE(flags.field4.Get());
    EXPECT_TRUE(flags.field5.IsSome());
    EXPECT_TRUE(flags.field5.Get());
    EXPECT_EQ("xxxx", flags.field6);
}

TEST(FlagsTest, InvalidNum)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field1=hello field1", "--field2=50", "--field3=false", "--field4=false",
                           "--field5=true",
                           "--field7=110",
                           "--field9=50",
                           "" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);
    EXPECT_FALSE(ret.IsNone());
}

TEST(FlagsTest, InvalidPath)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field1=hello field1", "--field2=50", "--field3=false", "--field4=false",
                           "--field5=true",
                           "--field7=100",
                           "--field8=/////asdfxsac/sdac/",
                           "--field9=50",
                           "" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);
    EXPECT_FALSE(ret.IsNone());
}

TEST(FlagsTest, ParseFlagsUnknownFlag)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test",   "--field1=hello field1", "--field2=50",
                           "--field3=false", "--field4=false",        "--field7=true" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_FALSE(ret.IsNone());
}

TEST(FlagsTest, ParseFlagsDuplicateFlag)
{
    TestFlagParser flags;
    int argc = 6;
    const char *argv[] = { "litebus-test",   "--field1=hello field1", "--field2=50",
                           "--field2=false", "--field4=false",        "--field5=true" };

    Option<string> ret = flags.ParseFlags(argc, argv);

    EXPECT_FALSE(ret.IsNone());
}

TEST(FlagsTest, UsageTest)
{
    class UsageTester : public virtual FlagParser {
    public:
        string field1;
        int field2;
        bool field3;
        Option<bool> field4;
        Option<bool> field5;
        string field6;
        UsageTester()
        {
            BUSLOG_INFO("ADD USage flag");
            AddFlag(&UsageTester::field1, "field1", "Set field1");

            AddFlag(&UsageTester::field2, "field2", "Set field2", 42);

            AddFlag(&UsageTester::field3, "field3", "Set field3", false);

            AddFlag(&UsageTester::field4, "field4", "Set field4");

            AddFlag(&UsageTester::field5, "field5", "Set field5");

            AddFlag(&UsageTester::field6, "field6", "Set field6", "xxxx");
        }
        ~UsageTester()
        {
        }
    };

    UsageTester ut;
    std::string ustr = ut.Usage();
    const std::string ustrExp =
        "usage:  [options]\n"
        " --[no-]help print usage message (default: false)\n"
        " --field1=VALUE Set field1 (default: )\n"
        " --field2=VALUE Set field2 (default: 42)\n"
        " --[no-]field3 Set field3 (default: false)\n"
        " --[no-]field4 Set field4\n"
        " --[no-]field5 Set field5\n"
        " --field6=VALUE Set field6 (default: xxxx)\n";
    EXPECT_EQ(ustrExp, ustr);

    const std::string ustrExp2 =
        "Hi, this is test Usage\n"
        "usage:  [options]\n"
        " --[no-]help print usage message (default: false)\n"
        " --field1=VALUE Set field1 (default: )\n"
        " --field2=VALUE Set field2 (default: 42)\n"
        " --[no-]field3 Set field3 (default: false)\n"
        " --[no-]field4 Set field4\n"
        " --[no-]field5 Set field5\n"
        " --field6=VALUE Set field6 (default: xxxx)\n";
    const std::string hellowString = "Hi, this is test Usage";
    std::string ustr2 = ut.Usage(hellowString);
    EXPECT_EQ(ustrExp2, ustr2);
}

TEST(FlagsTest, FlagsFromCmdLine)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field1=hello world", "--field2=20", "--field9=50",
                           "--no-field3",  "--no-field4",          "--field5" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_EQ(true, ret.IsNone());
    EXPECT_EQ("hello world", flags.field1);
    EXPECT_EQ(20, flags.field2);
    EXPECT_FALSE(flags.field3);
    EXPECT_TRUE(flags.field4.IsNone());
    EXPECT_TRUE(flags.field5.IsNone());
}

// there is no error but ignore field4 and field5
// when encounter "--", the parser will break
TEST(FlagsTest, FlagsWithNoNameButDoubleDash)
{
    TestFlagParser flags;
    const char *argv[] = { "litebus-test", "--field1=hello world", "--field2=20", "--no-field3", "--field9=50",
                           "--",           "--no-field4",          "--field5" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argv), argv);

    EXPECT_EQ(true, ret.IsNone());
    EXPECT_EQ("hello world", flags.field1);
    EXPECT_EQ(20, flags.field2);
    EXPECT_FALSE(flags.field3);
    EXPECT_TRUE(flags.field4.IsNone());
    EXPECT_TRUE(flags.field5.IsNone());
}

TEST(FlagsTest, FlagNumCheck)
{
    TestFlagParser flags;
    const char *argvNum[2049] = { "1" };

    Option<string> ret = flags.ParseFlags(ARRAY_SIZE(argvNum), argvNum);
    EXPECT_TRUE(ret.IsSome());
    BUSLOG_INFO(ret.Get());
}
