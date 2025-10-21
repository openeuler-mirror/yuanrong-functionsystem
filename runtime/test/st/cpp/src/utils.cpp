/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "utils.h"

std::string GetOutput(std::string cmd)
{
    FILE *fp;
    char buffer[1024];
    fp = popen(cmd.c_str(), "r");
    fgets(buffer, sizeof(buffer), fp);
    return std::string(buffer);
}

std::string GetPid(std::string keyword)
{
    std::string cmd = "ps -ef | grep " + keyword + " | grep -v grep | awk '{print $2}'";
    auto pid = GetOutput(cmd);
    auto pos = pid.find("\n");
    if (pos > -1) {
        pid.erase(pos, 1);
    }
    return pid;
}

std::string GetPpid(std::string keyword)
{
    std::string cmd = "ps -ef | grep " + keyword + " | grep -v grep | awk '{print $3}'";
    auto pid = GetOutput(cmd);
    auto pos = pid.find("\n");
    if (pos > -1) {
        pid.erase(pos, 1);
    }
    return pid;
}

void Kill(std::string pid)
{
    std::string cmd = "kill -9 " + pid;
    system(cmd.c_str());
}