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
from typing import Optional

from ray_adapter.util.placement_group import PlacementGroup


class PlacementGroupSchedulingStrategy:
    """Placement group based scheduling strategy.

    Attributes:
        placement_group: the placement group this actor belongs to,
            or None if it doesn't belong to any group.
        placement_group_bundle_index: the index of the bundle
            if the actor belongs to a placement group, which may be -1 to
            specify any available bundle.
        placement_group_capture_child_tasks: Not support yet.
    """

    def __init__(
            self,
            placement_group: "PlacementGroup",
            placement_group_bundle_index: int = -1,
            placement_group_capture_child_tasks: Optional[bool] = None,
    ):
        self.placement_group = placement_group
        self.placement_group_bundle_index = placement_group_bundle_index
        self.placement_group_capture_child_tasks = placement_group_capture_child_tasks


class NodeAffinitySchedulingStrategy:
    """Static scheduling strategy used to run a task or actor on a particular node.

    Attributes:
        node_id: the hex id of the node where the task or actor should run.
        soft: whether the scheduler should run the task or actor somewhere else
            if the target node doesn't exist (e.g. the node dies) or is infeasible
            during scheduling.
            If the node exists and is feasible, the task or actor
            will only be scheduled there.
            This means if the node doesn't have the available resources,
            the task or actor will wait indefinitely until resources become available.
            If the node doesn't exist or is infeasible, the task or actor
            will fail if soft is False
            or be scheduled somewhere else if soft is True.
        _spill_on_unavailable: Not support yet.
        _fail_on_unavailable: Not support yet.
    """

    def __init__(
            self,
            node_id: str,
            soft: bool,
            _spill_on_unavailable: bool = False,
            _fail_on_unavailable: bool = False,
    ):
        # This will be removed once we standardize on node id being hex string.
        if not isinstance(node_id, str):
            node_id = node_id.hex()

        self.node_id = node_id
        self.soft = soft
        self._spill_on_unavailable = _spill_on_unavailable
        self._fail_on_unavailable = _fail_on_unavailable

        self._validate_attributes()

    def _validate_attributes(self):
        """validate attributes"""
        if self._spill_on_unavailable and not self.soft:
            raise ValueError(
                "Invalid NodeAffinitySchedulingStrategy attribute. "
                "_spill_on_unavailable cannot be set when soft is "
                "False. Please set soft to True to use _spill_on_unavailable."
            )
        if self._fail_on_unavailable and self.soft:
            raise ValueError(
                "Invalid NodeAffinitySchedulingStrategy attribute. "
                "_fail_on_unavailable cannot be set when soft is "
                "True. Please set soft to False to use _fail_on_unavailable."
            )
