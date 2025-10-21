/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#pragma once

#include <string>

std::string GetOutput(std::string cmd);

std::string GetPid(std::string keyword);

std::string GetPpid(std::string keyword);

void Kill(std::string pid);