# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import ctypes
import importlib.metadata
import os
import platform
import subprocess
import sys
from importlib.metadata import PackageNotFoundError
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

    total_mem_gb = round(total_mem_kb / (1024**2), 2)
    return cpu_count, total_mem_gb


def get_linux_distribution():
    with open("/etc/os-release") as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith("ID="):
                return line.split('"')[1]
        return "Unknown"


def get_system_info():
    """获取系统架构信息"""
    # 系统类型
    system = platform.system()  # Linux, Windows, Darwin
    # 机器架构
    machine_arch = platform.machine()  # x86_64, arm64, aarch64, etc.
    # 处理器架构
    processor_arch = platform.processor()  # Intel x86, Other
    # 平台标识
    platform_str = platform.platform()  # 64bit
    # Python 编译时的架构
    platform_arch = platform.architecture()[0]
    return system, machine_arch, processor_arch, platform_str, platform_arch


def get_glibc_version():
    """使用 ctypes 获取 glibc 版本，获取失败时会抛出异常"""
    # 加载 libc 库
    libc = ctypes.CDLL(None)
    gnu_get_libc_version = libc.gnu_get_libc_version
    gnu_get_libc_version.restype = ctypes.c_char_p
    # 调用函数获取版本
    version = gnu_get_libc_version()
    if version:
        return version.decode("utf-8")
    else:
        return None


def check_package_metadata(package):
    try:
        version = importlib.metadata.version(package)
        return True, version
    except PackageNotFoundError:
        return False, None


def auto_install_and_upgrade(package):
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--upgrade", package])
    globals()[package] = __import__(package)

    return check_package_metadata(package)


def compare_version(version1, version2):
    """
    简化版本比较函数
    返回: 1表示version1 > version2, -1表示version1 < version2, 0表示相等
    """

    v1 = [int(x) for x in version1.split(".")]
    v2 = [int(x) for x in version2.split(".")]

    for i in range(max(len(v1), len(v2))):
        num1 = v1[i] if i < len(v1) else 0
        num2 = v2[i] if i < len(v2) else 0

        if num1 != num2:
            return 1 if num1 > num2 else -1

    return 0
