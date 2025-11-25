# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
from typing import Tuple


# 请勿在此声明全局变量


def pipeline_env() -> dict:
    """
    采集流水线环境变量信息
    """
    return {
        "BUILD_ID": os.getenv("BUILD_ID", ""),
        "BUILD_URL": os.getenv("BUILD_URL", ""),
        "JOB_URL": os.getenv("JOB_URL", ""),
        "JOB_BASE_NAME": os.getenv("JOB_BASE_NAME", ""),
        "RUN_DISPLAY_URL": os.getenv("RUN_DISPLAY_URL", ""),
    }


def get_linux_resources() -> Tuple[int, int]:
    """
    采集Linux环境CPU和MEM信息
    """
    with open("/proc/cpuinfo") as f:
        cpu_count = sum(1 for line in f if line.startswith("processor"))

    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemTotal"):
                total_mem_kb = int(line.split()[1])
                break

    total_mem_gb = round(total_mem_kb / (1024 ** 2), 2)
    return cpu_count, total_mem_gb


def get_linux_distribution():
    with open("/etc/os-release") as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith("ID="):
                return line.split('"')[1]
        return "Unknown"