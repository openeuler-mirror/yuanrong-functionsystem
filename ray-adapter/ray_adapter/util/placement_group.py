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
import logging
from typing import Dict, List, Optional, Union
import ray_adapter._private.state
from yr.apis import create_resource_group, remove_resource_group
from yr.resource_group_ref import RgObjectRef
from yr.resource_group import ResourceGroup
from yr.serialization import Serialization

_logger = logging.getLogger(__name__)

VALID_PLACEMENT_GROUP_STRATEGIES = {
    "PACK",
    "SPREAD",
    "STRICT_PACK",
    "STRICT_SPREAD",
}


class PlacementGroup:
    def __init__(self, resource_group: ResourceGroup):
        self._resource_group = resource_group

    def __eq__(self, other):

        if not isinstance(other, PlacementGroup):
            return False
        if self._resource_group is None or other._resource_group is None:
            return False
        return self.get_id() == other.get_id()

    def __hash__(self):
        return hash(self._resource_group.get_id())

    @property
    def resource_group(self):
        """resource_group property"""
        return self._resource_group

    def ready(self):
        """Returns an RgObjectRef to check ready status."""
        return RgObjectRef(self.resource_group, Serialization().serialize(self))

    def wait(self, timeout_seconds: Union[float, int] = 30) -> bool:
        """Wait for the placement group to be ready within the specified time."""
        try:
            self._resource_group.wait(int(timeout_seconds))
        except Exception as e:
            return False
        return True

    def get_id(self):
        """get group name"""
        if self._resource_group is None:
            return ""
        return self._resource_group.name


def _validate_bundles(bundles: List[Dict[str, float]]):
    """Validates each bundle and raises a ValueError if any bundle is invalid."""

    if not isinstance(bundles, list):
        raise ValueError(
            "Placement group bundles must be a list, " f"got {type(bundles)}."
        )

    if len(bundles) == 0:
        raise ValueError(
            "Bundles must be a non-empty list of resource "
            'dictionaries. For example: `[{"CPU": 300}, {"Memory": 128}]`. '
            "Got empty list instead."
        )

    for bundle in bundles:
        if (
                not isinstance(bundle, dict)
                or not all(isinstance(k, str) for k in bundle.keys())
                or not all(isinstance(v, (int, float)) for v in bundle.values())
        ):
            raise ValueError(
                "Bundles must be a non-empty list of "
                "resource dictionaries. For example: "
                '`[{"CPU": 300}, {"Memory": 128}]`.'
            )

        if len(bundle) == 0 or all(
                resource_value == 0 for resource_value in bundle.values()
        ):
            raise ValueError(
                "Bundles cannot be an empty dictionary or "
                f"resources with only 0 values. Bundles: {bundles}"
            )
        if "NPU" in bundle:
            bundle["NPU/.+/count"] = bundle.pop("NPU")
        if "GPU" in bundle:
            bundle["GPU/.+/count"] = bundle.pop("GPU")
        if "memory" in bundle:
            bundle["Memory"] = bundle.pop("memory")
        if "CPU" in bundle:
            bundle["CPU"] = bundle.pop("CPU") * 1000


def validate_placement_group(
        bundles: List[Dict[str, float]],
        strategy: str = "PACK",
        lifetime: Optional[str] = None,
        _soft_target_node_id: Optional[str] = None,
        bundle_label_selector: List[Dict[str, str]] = None,
) -> bool:
    """validate place group create params"""
    _validate_bundles(bundles)
    if strategy not in VALID_PLACEMENT_GROUP_STRATEGIES:
        raise ValueError(
            f"Invalid placement group strategy {strategy}. "
            f"Supported strategies are: {VALID_PLACEMENT_GROUP_STRATEGIES}."
        )

    if lifetime not in [None, "detached"]:
        raise ValueError(
            "Placement group `lifetime` argument must be either `None` or "
            f"'detached'. Got {lifetime}."
        )


def placement_group(
        bundles: List[Dict[str, float]],
        strategy: str = "PACK",
        name: str = "",
        lifetime: Optional[str] = None,
        _soft_target_node_id: Optional[str] = None,
        bundle_label_selector: List[Dict[str, str]] = None,
) -> PlacementGroup:
    """Asynchronously creates a PlacementGroup."""
    validate_placement_group(bundles, strategy, lifetime, _soft_target_node_id, bundle_label_selector)
    if len(name) == 0:
        return PlacementGroup(create_resource_group(bundles, None, strategy))
    return PlacementGroup(create_resource_group(bundles, name, strategy))


def remove_placement_group(placement_group_object: PlacementGroup) -> None:
    """Asynchronously remove placement group.

    Args:
        placement_group_object: The placement group to delete.
    """
    assert placement_group_object is not None
    remove_resource_group(placement_group_object.resource_group)


def placement_group_table(input_placement_group: PlacementGroup = None):
    """
    query placement group info
    """
    resource_group_name = input_placement_group.get_id() if (input_placement_group is not None) else None
    return ray_adapter._private.state.state.placement_group_table(resource_group_name)
