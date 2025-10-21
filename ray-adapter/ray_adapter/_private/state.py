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

from yr.apis import resource_group_table
from yr.common.types import CommonStatus, Resource, Resources, BundleInfo, Option, RgInfo, ResourceGroupUnit


def build_pg_dict(rg_info: RgInfo):
    """build pg dict"""
    rg_dict = {
        'placement_group_id': rg_info.name,
        'name': rg_info.name,
        'bundles': {},
        'stats': {
            'end_to_end_creation_latency_ms': 0.0,
            'scheduling_latency_ms': 0.0,
            'scheduling_attempt': 0.0,
            'highest_retry_delay_ms': 0.0,
            'scheduling_state': 'REMOVED'
        },
        'bundles_to_node_id': {}
    }

    for index, bundle in enumerate(rg_info.bundles):
        bundle_dict = {}
        for key, value in bundle.resources.resources.items():
            resource = value
            if resource.type == Resource.Type.SCALER:
                if "GPU" in resource.name:
                    bundle_dict["GPU"] = resource.scalar.value
                elif "NPU" in resource.name:
                    bundle_dict["NPU"] = resource.scalar.value
                elif "Memory" in resource.name:
                    bundle_dict["memory"] = resource.scalar.value
                else:
                    bundle_dict[resource.name] = resource.scalar.value
        rg_dict['bundles'][index] = bundle_dict
        rg_dict['bundles_to_node_id'][index] = bundle.functionProxyId

    rg_dict['state'] = {0: 'PENDING', 1: 'CREATED', 2: 'REMOVED'}.get(rg_info.status.code, 'UNKNOWN')
    rg_dict['strategy'] = {0: 'NONE', 1: 'SPREAD', 2: 'STRICTSPREAD', 3: 'PACK', 4: 'STRICTPACK'}.get(
        rg_info.opt.groupPolicy, 'UNKNOWN')

    return rg_dict


def parse_pg_info_to_dict(rg_unit: ResourceGroupUnit):
    """parse pg info to dict"""
    if len(rg_unit.resourceGroups) == 1:
        return build_pg_dict(next(iter(rg_unit.resourceGroups.values())))
    result = {}
    for key, value in rg_unit.resourceGroups.items():
        result[key] = build_pg_dict(value)
    return result


class GlobalState:
    def placement_group_table(self, placement_group_id=None):
        """
        query placement group info
        :param placement_group_id:
        :return: dict
        """
        return parse_pg_info_to_dict(resource_group_table(placement_group_id))


state = GlobalState()
