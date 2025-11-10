#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import sys
import json
import socket
import logging
from typing import Tuple
from typing import Optional
from urllib.request import urlopen
from urllib.request import Request


# 请勿在此声明全局变量

def init_logger() -> logging.Logger:
    """
    初始化日志组件
    """
    logging.basicConfig(
        level=logging.INFO,
        format='[%(levelname)s][%(asctime)s] %(message)s',
        datefmt='%b %d %H:%M:%S',
        handlers=[logging.StreamHandler(sys.stdout)]
    )
    log = logging.getLogger()
    return log


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


def report2es(headers: dict, data: dict) -> Optional[str]:
    """
    上报事件到ES存储
    """
    url = os.getenv("LOGSTASH_URL")
    if url is None:
        return None

    headers.update({
        'Content-Type': 'application/json',
        'User-Agent': socket.gethostname(),
    })
    json_data = json.dumps(data).encode("utf-8")
    req = Request(url=url, method="POST", headers=headers, data=json_data)

    try:
        rsp = urlopen(req)
    except (ConnectionRefusedError, ConnectionResetError) as e:
        return f"Connect [{url}] error: {e}"
    status_code = rsp.status
    if status_code != 200:
        return f"Failed to send logstash data: {data}"
    return None
