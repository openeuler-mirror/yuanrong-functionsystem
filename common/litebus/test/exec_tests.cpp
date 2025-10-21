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

#include <iostream>

#include <assert.h>
#include <gtest/gtest.h>
#include "exec/exec.hpp"
#include "exec/reap_process.hpp"
#include "executils.hpp"
#include "litebus.hpp"
#include "async/future.hpp"
#include <utils/os_utils.hpp>
#include <random>

using std::bind;
using std::function;
using std::map;
using std::string;

namespace litebus {
extern void NotifyPromise(pid_t pid, int result, int status);
}

namespace litebus {
namespace exectest {

class ExecTest : public TemporaryDirectoryTest {
};

Try<std::shared_ptr<Exec>> RunSubprocess(const std::function<Try<std::shared_ptr<Exec>>()> &createExec)
{
    Try<std::shared_ptr<Exec>> s = createExec();
    assert(s.Get() != nullptr);
    // Advance time until the internal reaper reaps the subprocess.
    while (s.Get()->GetStatus().IsInit()) {
        usleep(200 * 1000);
    };
    BUSLOG_INFO("future status finished, pid: {} exist?{}" , s.Get()->GetPid(), PidExist(s.Get()->GetPid()));

    return s;
}

void AwaitProcess(Try<std::shared_ptr<Exec>> s)
{
    while (s.Get()->GetStatus().IsInit()) {
        usleep(200 * 1000);
        BUSLOG_INFO("future status initing, pid: {} exist?{}" , s.Get()->GetPid(), PidExist(s.Get()->GetPid()));

    };
    BUSLOG_INFO("Await finished");
};

void OnSubprocessIORead(const std::string &cmd)
{
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec(cmd, None(), ExecIO::CreateFileIO("/dev/null"),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreatePipeIO());
}

TEST_F(ExecTest, FDInErrToDevnull)
{
    Try<std::shared_ptr<Exec>> s = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo goodbye 1>&2", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFDIO(STDOUT_FILENO), ExecIO::CreateFileIO("/dev/null"), {}, {});
    });
    EXPECT_GT(s.Get()->GetPid(), 0);
}

TEST_F(ExecTest, SubprocessIORead)
{
    std::string cmd = "echo 1000000000000";
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec(cmd, None(), ExecIO::CreateFileIO("/dev/null"),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreatePipeIO());
    sleep(2);
    EXPECT_TRUE(s.IsOK());
}

TEST_F(ExecTest, EnvironmentEcho)
{
    SetupDir();
    // Name the file to pipe output to.
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string outfile_name = "out_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".txt";
    const string outfile = GetTmpDir() + "/" + outfile_name;
    TouchFile(outfile);
    // Pipe simple string to output file.
    RunSubprocess([outfile]() -> Try<std::shared_ptr<Exec>> {
        const map<string, string> environment = { { "key1", "envirkey1" }, { "key2", "envirkey2" } };

        return Exec::CreateExec("echo $key2", environment, ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFileIO(outfile), ExecIO::CreateFDIO(STDERR_FILENO));
    });

    // Finally, read output file, and make sure message is inside.
    const Try<string> output = Read(outfile);
    EXPECT_EQ("envirkey2\n", output.Get());
    UnSetupDir();
}

TEST_F(ExecTest, PipeInput)
{
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("read word ; echo $word", None(), ExecIO::CreatePipeIO(),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    char buf[256];
    std::string write_str = "hellopipeinput\n";
    int r = ::write(s.Get()->GetIn().Get(), write_str.c_str(), write_str.length() + 1);
    EXPECT_GT(r, 0);
    r = ::read(s.Get()->GetOut().Get(), buf, sizeof(buf));
    std::string str = buf;
    BUSLOG_INFO("test read: {}", str);
    AwaitProcess(s);
    EXPECT_EQ(0, WEXITSTATUS(s.Get()->GetStatus().Get().Get()));
    EXPECT_GT(r, 0);
}

TEST_F(ExecTest, RuningPipeRead)
{
    Try<std::shared_ptr<Exec>> s =
        Exec::CreateExec("echo output1; sleep 1; echo output2;sleep 1;echo output3;", None(),
                         ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    int outputfd = s.Get()->GetOut().Get();
    sleep(1);
    Future<std::string> str = os::ReadPipeAsync(outputfd);
    BUSLOG_INFO("test read: {}", str.Get());
    AwaitProcess(s);
    EXPECT_EQ(str.Get(), "output1\noutput2\noutput3\n");
}

TEST_F(ExecTest, RuningPipeReadRealTime)
{
    Try<std::shared_ptr<Exec>> s =
        Exec::CreateExec("echo output1; sleep 1; echo output2;sleep 1;echo output3;", None(),
                         ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    int outputfd = s.Get()->GetOut().Get();
    sleep(1);
    auto promise = std::make_shared<litebus::Promise<std::string>>();
    Future<std::string> str = os::ReadPipeAsyncRealTime(outputfd, [promise](const std::string &content) {
        promise->SetValue(content);
    });
    BUSLOG_INFO("test read: {}", str.Get());
    AwaitProcess(s);
    EXPECT_STREQ(promise->GetFuture().Get().c_str(), "output1\n");
}

TEST_F(ExecTest, RuningPipeBigRead)
{
    SetupDir();
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string shellname = "echotest_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".sh";
    const string shellfile = GetTmpDir() + "/" + shellname;
    const int loopCount = 3000;
    const string loopStr = "Here we go again";
    const string endStr = "write end";
    TouchFile(shellfile);
    string echocmd =
        "COUNTER=1 \n MAXLEN=" + std::to_string(loopCount) + " \n while   [ ${COUNTER} -le ${MAXLEN} ]; do \n";
    echocmd.append("echo \"" + loopStr + "\" \n COUNTER=$(($COUNTER+1)) \n done \n echo \"" + endStr + "\"\n");
    EXPECT_GT(Write(shellfile, echocmd), 0);
    string chmod = "chmod 777 " + shellfile;
    int r = system(chmod.c_str());
    EXPECT_EQ(r, 0);
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("sh " + shellfile, None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    int outputfd = s.Get()->GetOut().Get();
    Future<std::string> str = os::ReadPipeAsync(outputfd);

    AwaitProcess(s);
    BUSLOG_INFO("read size:{}", str.Get().size());
    BUSLOG_INFO("read ening:{}", str.Get().substr(str.Get().size() - endStr.size() - 1, endStr.size()));
    EXPECT_EQ(str.Get().substr(str.Get().size() - endStr.size() - 1, endStr.size()), endStr);
    EXPECT_GT(str.Get().size(), loopCount * (loopStr.size() + 1) + endStr.size());
    UnSetupDir();
}

TEST_F(ExecTest, RuningPipeBigAbandonRead)
{
    SetupDir();
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string shellname = "echotest_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".sh";
    const string shellfile = GetTmpDir() + "/" + shellname;
    const int loopCount = 10240;
    const string loopStr = "Here we go again";
    const string endStr = "write end";
    TouchFile(shellfile);
    string echocmd =
        "COUNTER=1 \n MAXLEN=" + std::to_string(loopCount) + " \n while   [ ${COUNTER} -le ${MAXLEN} ]; do \n";
    echocmd.append("echo \"" + loopStr + "\" \n COUNTER=$(($COUNTER+1)) \n done \n echo \"" + endStr + "\"\n");
    EXPECT_GT(Write(shellfile, echocmd), 0);
    string chmod = "chmod 777 " + shellfile;
    int r = system(chmod.c_str());
    EXPECT_EQ(r, 0);
    BUSLOG_INFO("sh file {}", shellfile);
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("sh " + shellfile, None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    int outputfd = s.Get()->GetOut().Get();
    Future<std::string> str = os::ReadPipeAsync(outputfd);

    AwaitProcess(s);
    BUSLOG_INFO("read size:{}", str.Get().size());
    BUSLOG_INFO("read ening:{}", str.Get().substr(str.Get().size() - endStr.size() - 1, endStr.size()));
    EXPECT_EQ(str.Get().size(), os::BUFFER_CONTENT_SIZE);
    UnSetupDir();
}

static Future<Option<int>> KillPidReturn(pid_t pid, Future<Option<int>> f)
{
    std::shared_ptr<Promise<Option<int>>> promise(new Promise<Option<int>>());
    BUSLOG_INFO("pid was killed: {}", pid);
    if (f.IsOK()) {
        promise->SetValue(Option<int>(0));
    } else {
        promise->SetValue(Option<int>(-1));
    }
    ::kill(pid, 9);
    return promise->GetFuture();
}

TEST_F(ExecTest, RuningPipeBigAbandon300Read)
{
    SetupDir();
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string shellname = "echotest_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".sh";
    const string shellfile = GetTmpDir() + "/" + shellname;
    const int loopCount = 10240;
    const string loopStr = "Here we go again";
    const string endStr = "write end";
    TouchFile(shellfile);
    string echocmd =
        "COUNTER=1 \n MAXLEN=" + std::to_string(loopCount) + " \n while   [ ${COUNTER} -le ${MAXLEN} ]; do \n";
    echocmd.append("echo \"" + loopStr + "\" \n COUNTER=$(($COUNTER+1)) \n done \n echo \"" + endStr + "\"\n");
    EXPECT_GT(Write(shellfile, echocmd), 0);
    string chmod = "chmod 777 " + shellfile;
    int r = system(chmod.c_str());
    EXPECT_EQ(r, 0);
    const int procCount = 300;
    Try<std::shared_ptr<Exec>> s[procCount];
    Future<std::string> str[procCount];
    Future<Option<int>> flagFuture[procCount];
    for (int i = 0; i < procCount; i++) {
        s[i] = Exec::CreateExec("sh " + shellfile, None(), ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(),
                                ExecIO::CreateFDIO(STDERR_FILENO));
        pid_t pid = s[i].Get()->GetPid();
        flagFuture[i] =
            s[i].Get()->GetStatus().After(1000 * 40, std::bind(&KillPidReturn, pid, s[i].Get()->GetStatus()));
        int outputfd = s[i].Get()->GetOut().Get();
        str[i] = os::ReadPipeAsync(outputfd);
    }
    for (int i = 0; i < procCount; i++) {
        BUSLOG_INFO("i:{}", i);
        AwaitProcess(s[i]);
        EXPECT_EQ(flagFuture[i].Get().Get(), 0);
        EXPECT_EQ(str[i].Get().size(), os::BUFFER_CONTENT_SIZE);
    }
    UnSetupDir();
}

TEST_F(ExecTest, PipeErrorAndOutput)
{
    SetupDir();
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string shellname = "echotest_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".sh";
    const string shellfile = GetTmpDir() + "/" + shellname;
    TouchFile(shellfile);
    string echocmd = "echo this is output";
    EXPECT_GT(Write(shellfile, echocmd), 0);
    string chmod = "chmod 600 " + shellfile;
    int r = system(chmod.c_str());
    EXPECT_EQ(r, 0);
    Try<std::shared_ptr<Exec>> s;
    Future<std::string> strOutput;
    Future<std::string> strErr;
    s = Exec::CreateExec("sh " + shellfile + "aaa", None(), ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(),
                         ExecIO::CreatePipeIO());
    strOutput = os::ReadPipeAsync(s.Get()->GetOut().Get());
    strErr = os::ReadPipeAsync(s.Get()->GetErr().Get());
    BUSLOG_INFO("outstr: {}|", strOutput.Get());
    BUSLOG_INFO("errstr: {}|", strErr.Get());
    EXPECT_EQ(strOutput.Get().size(), (unsigned int)0);
    UnSetupDir();
}

TEST_F(ExecTest, RuningPipeReadSync)
{
    Try<std::shared_ptr<Exec>> s =
        Exec::CreateExec("echo output1; sleep 1; echo output2;sleep 2;echo output3;", None(),
                         ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    int outputfd = s.Get()->GetOut().Get();
    sleep(2);
    Future<std::string> str = os::ReadPipeAsync(outputfd, false);
    BUSLOG_INFO("test read: {}", str.Get());
    AwaitProcess(s);
    EXPECT_GT(str.Get(), "output1\noutput2");
}

TEST_F(ExecTest, PipeOutput)
{
    // Standard out.
    Try<std::shared_ptr<Exec>> s = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo hellopipeoutput1", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    });
    std::string str = os::ReadPipeAsync(s.Get()->GetOut().Get()).Get();
    BUSLOG_INFO("string read: {}", str);
    EXPECT_EQ("hellopipeoutput1\n", str);
    Try<std::shared_ptr<Exec>> s2 = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo hellopipeoutput2", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    });
    str = os::ReadPipeAsync(s2.Get()->GetOut().Get()).Get();
    BUSLOG_INFO("string read: {}", str);
    EXPECT_EQ("hellopipeoutput2\n", str);

    Try<std::shared_ptr<Exec>> s3 = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo hellopipeoutput3", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    });
    str = os::ReadPipeAsync(s3.Get()->GetOut().Get(), false).Get();
    BUSLOG_INFO("string read: {}", str);
    EXPECT_EQ("hellopipeoutput3\n", str);
    Try<std::shared_ptr<Exec>> s4 = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo hellopipeoutput4", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));
    });
    str = os::ReadPipeAsync(s4.Get()->GetOut().Get(), false).Get();
    BUSLOG_INFO("string read: {}", str);
    EXPECT_EQ("hellopipeoutput4\n", str);
}

TEST_F(ExecTest, PipeError)
{
    Try<std::shared_ptr<Exec>> s = RunSubprocess([]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo errorpipe 1>&2", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFDIO(STDOUT_FILENO), ExecIO::CreatePipeIO());
    });
    char buf[256];
    int r = read(s.Get()->GetErr().Get(), buf, sizeof(buf));
    std::string str = buf;
    BUSLOG_INFO("buf read: {}", buf);
    BUSLOG_INFO("string read: {}", str);
    EXPECT_GT(r, 0);
}

TEST_F(ExecTest, ReapSleep)
{
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("sleep 1");
    int status = WEXITSTATUS(s.Get()->GetStatus().Get().Get());
    BUSLOG_INFO("end sleep 1,status: {}", status);
    EXPECT_EQ(0, status);
}

TEST_F(ExecTest, ReapExit)
{
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("exit 1");
    AwaitProcess(s);
    int status = WEXITSTATUS(s.Get()->GetStatus().Get().Get());
    BUSLOG_INFO("end exit 1,status: {}", status);
    EXPECT_EQ(1, status);
}

TEST_F(ExecTest, PipeOutputToFileDescriptor)
{
    SetupDir();

    // Name the files to pipe output and error to.
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string outfile_name = "out_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".txt";
    const string outfile = GetTmpDir() + "/" + outfile_name;
    TouchFile(outfile);
    Try<int> outfile_fd = ::open(outfile.c_str(), O_RDWR, 0);
    BUSLOG_INFO("outfile FD dir: {}, r: {}", outfile, outfile_fd.Get());

    const string errorfile_name = "error.txt";
    const string errorfile = GetTmpDir() + "/" + errorfile_name;
    TouchFile(errorfile);
    Try<int> errorfile_fd = ::open(errorfile.c_str(), O_RDWR, 0);

    // Pipe simple string to output file.
    RunSubprocess([outfile_fd]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo hellopipetoFD", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFDIO(outfile_fd.Get()), ExecIO::CreateFDIO(STDERR_FILENO));
    });
    // Finally, read output and error files, and make sure messages are inside.
    const Try<string> output = Read(outfile);
    EXPECT_EQ("hellopipetoFD\n", output.Get());

    RunSubprocess([errorfile_fd]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo goodbye 1>&2", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFDIO(STDOUT_FILENO), ExecIO::CreateFDIO(errorfile_fd.Get()));
    });

    const Try<string> error = Read(errorfile);
    EXPECT_EQ("goodbye\n", error.Get());
    UnSetupDir();
}

TEST_F(ExecTest, FDinOutToPath)
{
    SetupDir();

    // Name the files to pipe output and error to.
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string outfile_name = "out_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".txt";
    const string outfile = GetTmpDir() + "/" + outfile_name;
    TouchFile(outfile);
    Try<int> outfile_fd = ::open(outfile.c_str(), O_RDWR, 0);
    BUSLOG_INFO("outfile FD dir: {}, r: {}", outfile, outfile_fd.Get());

    // Pipe simple string to output file.
    RunSubprocess([outfile]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo helloFDtopath", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFileIO(outfile), ExecIO::CreateFDIO(STDERR_FILENO));
    });

    // Finally, read output and error files, and make sure messages are inside.
    Close(outfile_fd.Get());
    const Try<string> output = Read(outfile);
    EXPECT_EQ("helloFDtopath\n", output.Get());
    UnSetupDir();
}

TEST_F(ExecTest, PathInput)
{
    SetupDir();
    const string infile_name = "in.txt";
    const string infile = GetTmpDir() + "/" + infile_name;
    TouchFile(infile);
    EXPECT_GT(Write(infile, "hellopathinput\0\n"), 0);

    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("read word ; echo $word", None(), ExecIO::CreateFileIO(infile),
                                                    ExecIO::CreatePipeIO(), ExecIO::CreateFDIO(STDERR_FILENO));

    char buf[15];
    int r = read(s.Get()->GetOut().Get(), buf, sizeof(buf));
    EXPECT_GT(r, 0);
    std::string str = buf;
    BUSLOG_INFO("buf read: {}", buf);
    BUSLOG_INFO("string read: {}, length: {}", str, str.length());
    EXPECT_EQ("hellopathinput", str.substr(0, 14));
    AwaitProcess(s);    // wait for reaper to reap
    UnSetupDir();
}

TEST_F(ExecTest, IOError)
{
    Try<std::shared_ptr<Exec>> s =
        Exec::CreateExec("exit 0", None(), ExecIO::CreateFileIO(".../NODEVICE/"), ExecIO::CreateFDIO(STDOUT_FILENO),
                         ExecIO::CreateFDIO(STDERR_FILENO));
    if (s.Get() == nullptr) {
        EXPECT_EQ(1, 1);
    } else {
        EXPECT_EQ(0, 1);
    }
    s = Exec::CreateExec("exit 0", None(), ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreateFDIO(-1),
                         ExecIO::CreateFDIO(STDERR_FILENO));
    if (s.Get() == nullptr) {
        EXPECT_EQ(1, 1);
    } else {
        EXPECT_EQ(0, 1);
    }
    s = Exec::CreateExec("exit 0", None(), ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreateFDIO(STDOUT_FILENO),
                         ExecIO::CreateFileIO(":::/NODEVICE/"));
    if (s.Get() == nullptr) {
        EXPECT_EQ(1, 1);
    } else {
        EXPECT_EQ(0, 1);
    }
}

void PrintVoid(std::string write_str)
{
    BUSLOG_INFO("myprint");
    int r = write(STDOUT_FILENO, write_str.c_str(), write_str.length() + 1);
    EXPECT_GT(r, 0);
}

TEST_F(ExecTest, ChildHookTest)
{
    std::string write_str = "myvoid";
    Try<std::shared_ptr<Exec>> s = RunSubprocess([=]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("exit 0", None(), ExecIO::CreateFDIO(STDIN_FILENO), ExecIO::CreatePipeIO(),
                                ExecIO::CreateFDIO(STDERR_FILENO),
                                { ChildInitHook::EXITWITHPARENT(), std::bind(&PrintVoid, write_str) }, {});
    });
    int status = WEXITSTATUS(s.Get()->GetStatus().Get().Get());
    BUSLOG_INFO("end exit 1,status: {}", status);
    char buf[10240];
    int r = read(s.Get()->GetOut().Get(), buf, sizeof(buf));
    EXPECT_GT(r, 0);
    std::string str = buf;
    BUSLOG_INFO("buf read: {}", buf);
    BUSLOG_INFO("string read: {}", str);
    EXPECT_EQ(write_str, str);
}

// this is to menual execute
TEST_F(ExecTest, TestParentExitWithChilid)
{
    std::string write_str = "myvoid";
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec(
        "sleep 4", None(), ExecIO::CreateFileIO("/dev/null"), ExecIO::CreatePipeIO(),
        ExecIO::CreateFileIO("/dev/null"), { ChildInitHook::EXITWITHPARENT(), std::bind(&PrintVoid, write_str) }, {});

    pid_t pid = s.Get()->GetPid();
    BUSLOG_INFO("sleep 30, child pid: {}", pid);
    EXPECT_EQ(PidExist(pid), true);
    // this is menual test for child exit with parent
    // to see if the child process still exit when parent process exit
    BUSLOG_INFO("Application exit");
}

TEST_F(ExecTest, ReapAKilledProcess)
{
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("sleep 2");
    pid_t pid = s.Get()->GetPid();
    BUSLOG_INFO("sleep 2, child pid: {}", pid);
    EXPECT_EQ(PidExist(pid), true);
    EXPECT_EQ(KillPid(pid), 0);
    AwaitProcess(s);    // wait for reaper to reap
    EXPECT_EQ(PidExist(pid), false);
    int status = WEXITSTATUS(s.Get()->GetStatus().Get().Get());
    EXPECT_EQ(0, status);
    BUSLOG_INFO("pid running: {}", PidExist(pid));
}

TEST_F(ExecTest, ExecBadCommand)
{
    SetupDir();

    // Name the files to pipe output and error to.
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(10000, 99999);
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    const string outfile_name = "out_" + std::to_string(dist(gen)) + "_" + std::to_string(timestamp % 100000) + ".txt";
    const string outfile = GetTmpDir() + "/" + outfile_name;
    TouchFile(outfile);
    Try<int> outfile_fd = ::open(outfile.c_str(), O_RDWR, 0);
    BUSLOG_INFO("outfile FD dir: {}, r: {}", outfile, outfile_fd.Get());

    const string errorfile_name = "error.txt";
    const string errorfile = GetTmpDir() + "/" + errorfile_name;
    TouchFile(errorfile);
    Try<int> errorfile_fd = ::open(errorfile.c_str(), O_RDWR, 0);

    Try<std::shared_ptr<Exec>> s = RunSubprocess([=]() -> Try<std::shared_ptr<Exec>> {
        return Exec::CreateExec("echo output; badcommand", None(), ExecIO::CreateFDIO(STDIN_FILENO),
                                ExecIO::CreateFDIO(outfile_fd.Get()), ExecIO::CreateFDIO(errorfile_fd.Get()));
    });
    const Try<string> output = Read(outfile);
    BUSLOG_INFO("string read output: {}", output.Get());
    const Try<string> error = Read(errorfile);
    BUSLOG_INFO("string read error: {}", error.Get());

    int status = WEXITSTATUS(s.Get()->GetStatus().Get().Get());
    BUSLOG_INFO("end sleep 1,status: {}", status);
    EXPECT_EQ(127, status);
    UnSetupDir();
}

TEST_F(ExecTest, ReapMulityProcess)
{
    Try<std::shared_ptr<Exec>> s1 = Exec::CreateExec("sleep 21");
    Try<std::shared_ptr<Exec>> s2 = Exec::CreateExec("sleep 22");
    Try<std::shared_ptr<Exec>> s3 = Exec::CreateExec("sleep 23");
    pid_t pid1 = s1.Get()->GetPid();
    pid_t pid2 = s2.Get()->GetPid();
    pid_t pid3 = s3.Get()->GetPid();
    EXPECT_EQ(PidExist(pid1), true);
    EXPECT_EQ(PidExist(pid2), true);
    EXPECT_EQ(PidExist(pid3), true);
    EXPECT_EQ(KillPid(pid1), 0);
    EXPECT_EQ(KillPid(pid2), 0);
    EXPECT_EQ(KillPid(pid3), 0);
    AwaitProcess(s1);    // wait for reaper to reap
    AwaitProcess(s2);
    AwaitProcess(s3);
    EXPECT_EQ(PidExist(pid1), false);
    EXPECT_EQ(PidExist(pid2), false);
    EXPECT_EQ(PidExist(pid3), false);
}

TEST_F(ExecTest, ReapSleepMulityOneExec)
{
    Try<std::shared_ptr<Exec>> s;
    const int times = 10;
    pid_t pids[times];
    for (int i = 0; i < times; i++) {
        s = Exec::CreateExec("sleep 10");
        pids[i] = s.Get()->GetPid();
    }
    for (int i = 0; i < times; i++) {
        EXPECT_EQ(PidExist(pids[i]), true);
    }
    for (int i = 0; i < times; i++) {
        EXPECT_EQ(KillPid(pids[i]), 0);
    }
    AwaitProcess(s);    // wait for reaper to reap
    for (int i = 0; i < times; i++) {
        EXPECT_EQ(PidExist(pids[i]), false);
    }
}

TEST_F(ExecTest, ReapNotExistMulityOneExec)
{
    const int times = 100;
    pid_t pids[times];
    for (int j = 0; j < times; j++) {
        Try<std::shared_ptr<Exec>> s = Exec::CreateExec("reapnosh.sh");
        pids[j] = s.Get()->GetPid();
        EXPECT_EQ(PidExist(pids[j]), true);
    }
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("reapnosh.sh");
    AwaitProcess(s);
    usleep(400);
    for (int j = 0; j < times; j++) {
        EXPECT_EQ(PidExist(pids[j]), false);
    }
}

TEST_F(ExecTest, ReapMulityOneExecsh)
{
    SetupDir();
    const string infile_name = "reapsh.sh";
    const string infile = GetTmpDir() + "/" + infile_name;
    TouchFile(infile);
    EXPECT_GT(Write(infile, "sleep 1\n"), 0);
    BUSLOG_INFO("sh file {}/{}", GetTmpDir(), infile_name);
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec("sh " + GetTmpDir() + "/" + infile_name);
    pid_t pid1 = s.Get()->GetPid();
    s = Exec::CreateExec("sh " + GetTmpDir() + "/" + infile_name);
    pid_t pid2 = s.Get()->GetPid();
    AwaitProcess(s);
    EXPECT_EQ(PidExist(pid1), false);
    EXPECT_EQ(PidExist(pid2), false);
    UnSetupDir();
}

TEST_F(ExecTest, ReapBadExecshWait)
{
    SetupDir();
    const string infile_name = "reapsh.sh";
    const string infile = GetTmpDir() + "/" + infile_name;
    TouchFile(infile);
    EXPECT_GT(Write(infile, "sleep 5\n"), 0);
    BUSLOG_INFO("sh file {}/{}", GetTmpDir(), infile_name);
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec(GetTmpDir() + "/" + infile_name);
    pid_t pid1 = s.Get()->GetPid();
    s = Exec::CreateExec(GetTmpDir() + "/" + infile_name);
    pid_t pid2 = s.Get()->GetPid();
    AwaitProcess(s);
    EXPECT_EQ(PidExist(pid1), false);
    EXPECT_EQ(PidExist(pid2), false);
    UnSetupDir();
}

TEST_F(ExecTest, ReapBadExecshExitDirectly)
{
    SetupDir();
    const string infile_name = "reapsh.sh";
    BUSLOG_INFO("sh file {}/{}", GetTmpDir(), infile_name);
    Try<std::shared_ptr<Exec>> s = Exec::CreateExec(GetTmpDir() + "/a" + infile_name);
    pid_t pid1 = s.Get()->GetPid();
    EXPECT_EQ(PidExist(pid1), true);
    UnSetupDir();
}

void HookFunc(int *r)
{
    *r = *r + 1;
}

// this is to menual execute
TEST_F(ExecTest, FuncTest)
{
    int p = 1;
    int *r = &p;
    std::string write_str = "myvoid";
    Try<InFileDescriptor> ins = ExecIO::CreateFDIO(STDIN_FILENO).inputSetup();
    Try<OutFileDescriptor> outs = ExecIO::CreateFDIO(STDOUT_FILENO).outputSetup();
    Try<OutFileDescriptor> errs = ExecIO::CreateFDIO(STDERR_FILENO).outputSetup();
    execinternal::HandleIOAndHook(ins.Get(), outs.Get(), errs.Get(),
                                  { ChildInitHook::EXITWITHPARENT(), std::bind(&HookFunc, r) });
    BUSLOG_INFO("r1: {}", p);
    EXPECT_EQ(p, 2);

    // run with error io
    ins = ExecIO::CreateFDIO(-1).inputSetup();
    outs = ExecIO::CreateFDIO(-1).outputSetup();
    errs = ExecIO::CreateFDIO(-1).outputSetup();
    p = 3;
    EXPECT_EQ(ins.IsOK(), false);
    EXPECT_EQ(outs.IsOK(), false);
    EXPECT_EQ(errs.IsOK(), false);
}

TEST_F(ExecTest, NotifyPromiseTest)
{
    ReaperActor *actor = new ReaperActor("test");
    EXPECT_TRUE(actor != nullptr);

    litebus::NotifyPromise(0, 0, 0);

    delete actor;
}

}    // namespace exectest
}    // namespace litebus
