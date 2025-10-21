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
import unittest
from unittest.mock import patch, Mock

from ray_adapter.util.placement_group import (placement_group, remove_placement_group, PlacementGroup)
from yr.resource_group import ResourceGroup


class TestPlacementGroup(unittest.TestCase):

    @patch("yr.apis.is_initialized")
    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_create_resource_group_success(self, get_runtime, is_initialized):
        is_initialized.return_value = True
        mock_runtime = Mock()
        mock_runtime.create_resource_group.return_value = "return_req_id"
        get_runtime.return_value = mock_runtime
        res = placement_group([{"CPU": 500}])
        self.assertTrue(res.resource_group.request_id == "return_req_id")

    @patch("yr.apis.is_initialized")
    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_remove_placement_group_success(self, get_runtime, is_initialized):
        placement_group = PlacementGroup(ResourceGroup("name", id))
        mock_runtime = Mock()
        mock_runtime.remove_resource_group.return_value = "OK"
        get_runtime.return_value = mock_runtime
        try:
            remove_placement_group(placement_group)
        except Exception as e:
            assert False, f"Unexpected exception raised: {e}"


if __name__ == "__main__":
    unittest.main()
