# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import logging
import os
import socket
import sys
from typing import Optional
from urllib.request import Request, urlopen


def stream_logger() -> logging.Logger:
    """
    初始化日志组件
    """
    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s] %(message)s",
        datefmt="%b %d %H:%M:%S",
        handlers=[logging.StreamHandler(sys.stdout)],
    )
    log = logging.getLogger()
    return log


def report2es(headers: dict, data: dict) -> Optional[str]:
    """
    上报事件到ES存储
    """
    url = os.getenv("LOGSTASH_URL")
    if url is None:
        return None

    headers.update(
        {
            "Content-Type": "application/json",
            "User-Agent": socket.gethostname(),
        }
    )
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
