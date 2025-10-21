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

from typing import List
import yr.apis


def get_node_ip_address(address=""):
    """
    Obtain the node ip.
    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> node_ip = ray.util.get_node_ip_address()
        >>> print(node_ip)
    """
    return yr.apis.get_node_ip_address()


def list_named_actors(all_namespaces: bool = False) -> List[str]:
    """
    list named actor.
    Examples:
        >>> import yr.ray_adapter as ray
        >>> ray.init()
        >>> named_actors = ray.list_named_actors()
        >>> print(named_actors)
    """
    return yr.apis.list_named_instances(all_namespaces)
