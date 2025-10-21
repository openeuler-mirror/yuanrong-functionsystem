#ifndef __EXEC_TESTS_UTILS_H__
#define __EXEC_TESTS_UTILS_H__

#include <errno.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <actor/buslog.hpp>
#include <regex>
#include <string>
#include <utils/os_utils.hpp>
#include <utils/string_utils.hpp>

#include "executils.hpp"
#include "iomgr/evbufmgr.hpp"
#include "utils/time_util.hpp"

using namespace std;

namespace litebus {
extern void SetAdvertiseAddr(const std::string &advertiseUrl);
extern std::string EncodeHttpMsg(MessageBase *msg);
namespace os {
extern Option<int> Chown(uid_t uid, gid_t gid, const std::string &path, bool recursive);
}
}    // namespace litebus

namespace litebus {
namespace utilstest {

inline int Close(int fd)
{
    if (fd >= 0) {
        return ::close(fd);
    } else {
        return 0;
    }
}

// wirte to a fd
inline int Write(int fd, const char *buffer, size_t count)
{
    size_t offset = 0;
    while (offset < count) {
        ssize_t length = ::write(fd, buffer + offset, count - offset);
        offset += length;
    }
    return offset;
}

inline int Write(const std::string &path, const std::string &message)
{
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (fd < 0) {
        return fd;
    }
    int r = Write(fd, message.data(), message.size());
    Close(fd);
    return r;
}

static std::string GetCWD()
{
    size_t size = 100;

    while (true) {
        char *temp = new char[size];
        if (::getcwd(temp, size) == temp) {
            std::string result(temp);
            delete[] temp;
            return result;
        } else {
            if (errno != ERANGE) {
                delete[] temp;
                return std::string();
            }
            size *= 2;
            delete[] temp;
        }
    }

    return std::string();
}

inline bool FileExists(const std::string &path)
{
    struct stat s;

    if (::lstat(path.c_str(), &s) < 0) {
        return false;
    }
    return true;
}

inline int TouchFile(const std::string &path)
{
    if (!FileExists(path)) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

        if (fd > 0) {
            return Close(fd);
        } else {
            return fd;
        }
    }
    // exist alreay
    return 0;
}

class OsUtilTest : public ::testing::Test {
protected:
    virtual void SetUp()
    {
        BUSLOG_INFO("start");
        if (os::ExistPath(GetTmpDir())) {
            os::Rmdir(GetTmpDir());
        }
    }

    virtual void TearDown()
    {
        BUSLOG_INFO("stop");
    }

    // A temporary directory for test purposes.
    // Not to be confused with the "sandbox" that tasks are run in.

private:
    std::string cur_dir = "";

    std::string tmp_folder = "tmp";

    std::string tmpdir = "tmp";

protected:
    std::string GetTmpDir()
    {
        return tmpdir;
    }

    inline void SetupDir()
    {
        // Save the current working directory.
        BUSLOG_INFO("Will SetupDir");
        std::string cwd = GetCWD();
        cur_dir = (cur_dir == "") ? cwd : cur_dir;
        tmpdir = os::Join(cur_dir, tmp_folder);
        BUSLOG_INFO("tmp dir create: {}", GetTmpDir());
        // Run the test out of the temporary directory we created.
    }
};

template <typename T>
bool Contains(std::vector<T> vec, T v)
{
    return find(vec.begin(), vec.end(), v) != vec.end();
}

TEST_F(OsUtilTest, osfuncTest)
{
    const string infile_name = "in.txt";
    const string infile = GetTmpDir() + "/" + infile_name;

    EXPECT_EQ(os::ExistPath(infile), false);
    EXPECT_EQ(os::ExistPath(GetTmpDir()), false);
    os::Mkdir("/");
    os::Mkdir(GetTmpDir());
    EXPECT_EQ(os::ExistPath(GetTmpDir()), true);

    EXPECT_EQ(os::ExistPath(infile), false);
    TouchFile(infile);
    Write(infile, "teststring");
    Option<std::string> str = os::Read(infile);
    EXPECT_EQ("teststring", str.Get());

    Option<std::vector<std::string>> flist = os::Ls(GetTmpDir());
    EXPECT_EQ(flist.Get().size(), (unsigned int)1);

    EXPECT_EQ(os::ExistPath(infile), true);
    os::Rm(infile);

    const string shfile_name = "shtest.sh";
    const string shfile = GetTmpDir() + "/" + infile_name;

    Option<int> r = os::Mkdir(GetTmpDir() + "/a/b/c/d", true);
    r = os::Mkdir(GetTmpDir() + "/a/b1/c/d", true);
    r = os::Mkdir(GetTmpDir() + "/a/b2/c/d", true);
    EXPECT_EQ(os::ExistPath(GetTmpDir() + "/a/b/c/d"), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d/e", true);
    TouchFile(GetTmpDir() + "/a/b/c/d/e" + "/a.txt");
    TouchFile(GetTmpDir() + "/a/b/c/d/e" + "/b.txt");
    TouchFile(GetTmpDir() + "/a/b/c/d/e" + "/f.txt");
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d/e/f1", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d/e/f2", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d1", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d1/e1", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d2", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d", false);
    EXPECT_EQ(r.IsNone(), true);

    EXPECT_EQ(os::ExistPath(GetTmpDir() + "/a/b/c"), true);
    Option<std::vector<std::string>> dirs = os::Ls(GetTmpDir() + "/a/b/c");
    EXPECT_EQ(dirs.IsSome(), true);
    EXPECT_EQ(dirs.Get().size(), (unsigned int)3);
    EXPECT_EQ(Contains<std::string>(dirs.Get(), "d"), true);
    EXPECT_EQ(Contains<std::string>(dirs.Get(), "d1"), true);
    EXPECT_EQ(Contains<std::string>(dirs.Get(), "d2"), true);
    EXPECT_EQ(Contains<std::string>(dirs.Get(), "."), false);
    EXPECT_EQ(Contains<std::string>(dirs.Get(), ".."), false);

    r = os::Rmdir(GetTmpDir() + "/a/b/c/d/e/f.txt", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Rmdir(GetTmpDir() + "/a/b/c/d2", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Rmdir(GetTmpDir() + "/a/b/c", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Rmdir(GetTmpDir() + "/a/b", true);
    EXPECT_EQ(r.IsNone(), true);
    EXPECT_EQ(os::ExistPath(GetTmpDir() + "/a/b"), false);
    r = os::Rmdir(GetTmpDir());
    EXPECT_EQ(r.IsNone(), true);
    EXPECT_EQ(os::ExistPath(GetTmpDir()), false);
    TouchFile(shfile);
    Write(shfile, "pwd");
    Option<int> cr = os::Chown("root", shfile, true);
    cr = os::Chown("rootabcde", shfile, true);
    EXPECT_EQ(cr.IsNone(), true);

    std::map<std::string, std::string> oldEnvs = os::Environment();

    os::SetEnv("TESTENV", "testvalue");
    std::map<std::string, std::string> newEnvs = os::Environment();
    EXPECT_EQ(oldEnvs.size() + 1, newEnvs.size());
    EXPECT_EQ(os::GetEnv("TESTENV").Get(), "testvalue");
    os::UnSetEnv("TESTENV");
    EXPECT_EQ(os::GetEnv("TESTENV").IsNone(), true);

    string longEnv(1281, 'x');
    os::SetEnv("TESTENV", longEnv);
    EXPECT_EQ(os::GetEnv("TESTENV").IsNone(), true);

    string s = os::Strerror(12);
    BUSLOG_INFO("errno 12: {}", s);
    EXPECT_EQ(s.length() > 0, true);
}

TEST_F(OsUtilTest, stringsFuncTest)
{
    string sourcestr = "a==ab==abc==abcd";
    vector<string> strlist = strings::Split(sourcestr, "==");
    EXPECT_EQ((unsigned int)4, strlist.size());
    EXPECT_EQ("a", strlist[0]);
    EXPECT_EQ("ab", strlist[1]);
    EXPECT_EQ("abc", strlist[2]);
    EXPECT_EQ("abcd", strlist[3]);

    strlist = strings::Split(sourcestr, "==", 2);
    EXPECT_EQ((unsigned int)2, strlist.size());
    EXPECT_EQ("a", strlist[0]);
    EXPECT_EQ("ab==abc==abcd", strlist[1]);

    strlist = strings::Split("abc", "=", 2);
    EXPECT_EQ((unsigned int)1, strlist.size());
    EXPECT_EQ("abc", strlist[0]);

    strlist = strings::Tokenize("=abc===abc==a==bc", "=", 0);
    EXPECT_EQ((unsigned int)4, strlist.size());
    EXPECT_EQ("abc", strlist[0]);
    EXPECT_EQ("abc", strlist[1]);
    EXPECT_EQ("a", strlist[2]);
    EXPECT_EQ("bc", strlist[3]);

    strlist = strings::Tokenize("=abc===abc==a==bc", "=", 3);
    EXPECT_EQ((unsigned int)3, strlist.size());
    EXPECT_EQ("abc", strlist[0]);
    EXPECT_EQ("abc", strlist[1]);
    EXPECT_EQ("a==bc", strlist[2]);

    string trimstr1 = "       ";
    string trimstr2 = "  create";
    string trimstr3 = "create  ";
    string trimstr4 = " create ";

    string trimret;
    trimret = strings::Trim(trimstr1);
    EXPECT_EQ((unsigned int)0, trimstr1.size());
    EXPECT_EQ(trimret, trimstr1);

    // test prefix
    trimret = strings::Trim(trimstr2, strings::PREFIX);
    EXPECT_EQ((unsigned int)6, trimstr2.size());
    EXPECT_EQ(trimret, trimstr2);

    trimstr2 = "  create";
    trimret = strings::Trim(trimstr2, strings::SUFFIX);
    EXPECT_EQ((unsigned int)8, trimstr2.size());
    EXPECT_EQ(trimret, trimstr2);

    trimstr2 = "  create";
    trimret = strings::Trim(trimstr2);
    EXPECT_EQ((unsigned int)6, trimstr2.size());
    EXPECT_EQ(trimret, trimstr2);

    // test suffix
    trimret = strings::Trim(trimstr3, strings::PREFIX);
    EXPECT_EQ((unsigned int)8, trimstr3.size());
    EXPECT_EQ(trimret, trimstr3);

    trimstr3 = "create  ";
    trimret = strings::Trim(trimstr3, strings::SUFFIX);
    EXPECT_EQ((unsigned int)6, trimstr3.size());
    EXPECT_EQ(trimret, trimstr3);

    trimstr3 = "create  ";
    trimret = strings::Trim(trimstr3);
    EXPECT_EQ((unsigned int)6, trimstr3.size());
    EXPECT_EQ(trimret, trimstr3);

    // test all
    trimret = strings::Trim(trimstr4, strings::PREFIX);
    EXPECT_EQ((unsigned int)7, trimstr4.size());
    EXPECT_EQ(trimret, trimstr4);

    trimstr4 = " create ";
    trimret = strings::Trim(trimstr4, strings::SUFFIX);
    EXPECT_EQ((unsigned int)7, trimstr4.size());
    EXPECT_EQ(trimret, trimstr4);

    trimstr4 = " create ";
    trimret = strings::Trim(trimstr4);
    EXPECT_EQ((unsigned int)6, trimstr4.size());
    EXPECT_EQ(trimret, trimstr4);
}

TEST_F(OsUtilTest, logCheck)
{
    int num = 0;
    int cheknum = 0;
    while (num < 100000) {
        num++;

        if (LOG_CHECK_EVERY_N()) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(46, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 20) {
        num++;

        if (LOG_CHECK_EVERY_N1(1, 10)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(3, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 20) {
        num++;
        if (LOG_CHECK_EVERY_N1(10, 10)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(11, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 200) {
        num++;
        if (LOG_CHECK_EVERY_N2(10, 10, 100)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(20, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 20000) {
        num++;
        if (LOG_CHECK_EVERY_N3(10, 10, 100, 1000)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(47, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 200000) {
        num++;
        if (LOG_CHECK_EVERY_N4(10, 10, 100, 1000, 10000)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(56, cheknum);

    num = 0;
    cheknum = 0;
    while (num < 10) {
        num++;
        if (LOG_CHECK_FIRST_N(5)) {
            cheknum++;
            //  VLOG(0) << "num:" <<num ;
        }
    }

    BUSLOG_DEBUG("checknum: {}", cheknum);
    EXPECT_EQ(5, cheknum);
}

TEST_F(OsUtilTest, RemoveTest)
{
    auto res = litebus::strings::Remove("hello world", "hello", litebus::strings::PREFIX);
    EXPECT_EQ(res, " world");
    BUSLOG_INFO("result = {}", res);

    res = litebus::strings::Remove("hello world", "hello", litebus::strings::SUFFIX);
    EXPECT_EQ(res, "hello world");
    BUSLOG_INFO("result = {}", res);

    res = litebus::strings::Remove("hello world", "hello", litebus::strings::ANY);
    EXPECT_EQ(res, " world");
    BUSLOG_INFO("result = {}", res);
}

TEST_F(OsUtilTest, EncodeHttpMsgTest)
{
    MessageBase *msg1 = new litebus::MessageBase("TestActor1", "TestActor2", "test_f", "dadsfdasf");
    auto res = EncodeHttpMsg(msg1);
    EXPECT_TRUE(!res.empty());
    BUSLOG_INFO("result = {}", res);

    MessageBase *msg2 = new litebus::MessageBase("TestActor1", "", "test_f", "dadsfdasf");
    res = EncodeHttpMsg(msg2);
    EXPECT_TRUE(!res.empty());
    BUSLOG_INFO("result = {}", res);

    delete msg1;
    delete msg2;
}

TEST_F(OsUtilTest, JoinTest)
{
    SetAdvertiseAddr("tcp://127.0.0.1:2224");

    SetAdvertiseAddr("127.0.0.1:2224");

    string res = os::Join(GetCWD(), "tmp");
    EXPECT_TRUE(!res.empty());
    BUSLOG_INFO("result = {}", res);
}

TEST_F(OsUtilTest, ChownTest)
{
    const string infile_name = "in.txt";
    const string shfile_name = "shtest.sh";
    const string shfile = GetTmpDir() + "/" + infile_name;

    Option<int> r = os::Mkdir(GetTmpDir() + "/a/b/c/d", true);
    EXPECT_EQ(os::ExistPath(GetTmpDir() + "/a/b/c/d"), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d", true);
    EXPECT_EQ(r.IsNone(), true);
    r = os::Mkdir(GetTmpDir() + "/a/b/c/d", false);
    EXPECT_EQ(r.IsNone(), true);

    os::Rmdir(GetTmpDir());
    EXPECT_EQ(os::ExistPath(GetTmpDir()), false);
    TouchFile(shfile);
    Write(shfile, "pwd");
    Option<int> cr = os::Chown(0, 0, shfile, true);
    EXPECT_EQ(cr.IsNone(), true);
}

TEST_F(OsUtilTest, ReadPipeAsyncTest)
{
    auto res = os::ReadPipeAsync(-1);
    EXPECT_TRUE(!res.IsOK());
}

class SensitiveValueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(SensitiveValueTest, ConstructorTest)
{
    SensitiveValue invalid;  // default
    EXPECT_TRUE(invalid.Empty());
    EXPECT_EQ(0, invalid.GetSize());
    EXPECT_EQ("", std::string(invalid.GetData()));

    SensitiveValue invalid1;           // default
    EXPECT_TRUE(invalid1 == invalid);  // size == 0

    const char *str = "";
    SensitiveValue valid(str);  // char *
    EXPECT_TRUE(valid.Empty());

    const char *str1 = "c";
    SensitiveValue valid1(str1, 1);  // char *
    EXPECT_FALSE(valid1.Empty());
    EXPECT_FALSE(valid == valid1);

    SensitiveValue valid2 = valid1;
    EXPECT_TRUE(valid2 == valid1);
    valid2.Clear();

    SensitiveValue valid3 = std::move(valid1);
    EXPECT_EQ(0, valid1.GetSize()); // =0
    EXPECT_FALSE(valid3 == valid1);

    SensitiveValue valid4("t");
    EXPECT_EQ("t", std::string(valid4.GetData()));
}

TEST_F(SensitiveValueTest, MoveTest)
{
    SensitiveValue value;

    size_t outSize;
    std::unique_ptr<char[]> outData(new char[4]);
    EXPECT_FALSE(value.MoveTo(outData, outSize));

    value = "test";
    EXPECT_EQ(4, value.GetSize());
    EXPECT_TRUE(value.MoveTo(outData, outSize));
}

TEST_F(SensitiveValueTest, OperatorTest)
{
    SensitiveValue value1("test");

    SensitiveValue value2 = value1;
    EXPECT_EQ("test", std::string(value2.GetData()));

    const char *string = "test-c";
    value2 = string;  // operator=(const char *str)
    EXPECT_EQ("test-c", std::string(value2.GetData()));

    value2 = "test-s";  // operator=(const std::string &str)
    EXPECT_EQ("test-s", std::string(value2.GetData()));
}

class StringUtilTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(StringUtilTest, SHA256AndHexTest)
{
    std::stringstream ss;
    hmac::SHA256AndHex("test-data", ss);
    EXPECT_EQ("a186000422feab857329c684e9fe91412b1a5db084100b37a98cfc95b62aa867\n", ss.str());
}

TEST_F(StringUtilTest, HMACAndSHA256Test)
{
    std::string data("test-data");
    SensitiveValue secret("test-secret");  // std::string
    EXPECT_EQ("8a8acf441916268bc4ad5f8f04e914a270ac0c2fc931f42e99dcfb41e9291463", hmac::HMACAndSHA256(secret, data));
}

class TimeUtilTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(TimeUtilTest, GetCurrentUTCTimeTest)
{
    std::regex pattern("^\\d{8}T\\d{6}Z$");  // 20250103T075746Z
    EXPECT_TRUE(std::regex_match(time::GetCurrentUTCTime(), pattern));
}

}    // namespace utilstest
}    // namespace litebus

#endif    // __EXEC_TESTS_UTILS_H__
