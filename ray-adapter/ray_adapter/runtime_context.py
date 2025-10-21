#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os
import threading
import yr


class RuntimeContext(object):
    """A class used for getting runtime context."""

    def __init__(self):
        """"""
        pass

    @property
    def namespace(self):
        """
        Get the namespace.

        Returns:
            str: "".

        Examples:
            >>> import ray_adapter as ray
            >>> ray.init()
            >>> result = ray.runtime_context().namespace
            >>> print(result)
        """
        return yr.apis.get_namespace()

    def get_accelerator_ids(self):
        """
        Get the IDs of available accelerators.

        This function retrieves the IDs of NPU and other devices
        based on environment variables. It returns a dictionary containing
        the IDs of the accelerators.

        Returns:
            dict: A dictionary with keys "NPU" and "device", each containing
                  a list of corresponding accelerator IDs.

        Example:
            >>> import ray_adapter as ray
            >>> ray.init()
            >>> result = ray.runtime_context().get_accelerator_ids()
            >>> print(result)
        """
        accelerator_ids = {}

        npu_info = None
        if os.getenv("YR_NOSET_ASCEND_RT_VISIBLE_DEVICES") == "1":
            npu_info = os.getenv("NPU-DEVICE-IDS")
        else:
            npu_info = os.getenv("ASCEND_RT_VISIBLE_DEVICES") or os.getenv("NPU-DEVICE-IDS")

        if npu_info:
            accelerator_ids["NPU"] = npu_info.split(",")
        else:
            accelerator_ids["NPU"] = []

        device_info = os.getenv("DEVICE_INFO")
        if device_info:
            accelerator_ids["device"] = device_info.split(",")
        else:
            accelerator_ids["device"] = []

        return accelerator_ids

    def get_node_id(self):
        """
        Get the ID of the current node.

        This function calls yr.apis.get_node_id() to obtain the unique identifier of the current node.

        Returns:
            str: The unique identifier of the node.

        Examples:
            >>> import ray_adapter as ray
            >>> ray.init()
            >>> result = ray.runtime_context().get_node_id()
            >>> print(result)
        """
        node_id = yr.apis.get_node_id()
        return node_id


_runtime_context = None
_runtime_context_lock = threading.Lock()


def get_runtime_context() -> RuntimeContext:
    """
    """
    with _runtime_context_lock:
        global _runtime_context
        if _runtime_context is None:
            _runtime_context = RuntimeContext()

        return _runtime_context
