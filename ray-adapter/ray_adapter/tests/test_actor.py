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

import yr
import ray_adapter.actor
import ray_adapter.util
from ray_adapter.actor import build_yr_scheduling_options

from ray_adapter.util.placement_group import placement_group


class TestProxyAdaptor(unittest.TestCase):

    def test_ray_function_proxy(self):
        mock_function_proxy = Mock()
        mock_function_proxy.options.return_value = "options"
        mock_function_proxy.invoke.return_value = "remote"

        ray_func_proxy = ray_adapter.actor.RemoteFunction(mock_function_proxy)
        ray_func_proxy.options(1)
        res = ray_func_proxy.remote(1)
        self.assertEqual(res, "remote")

    def test_ray_method_proxy(self):
        mock_method_proxy = Mock()
        mock_method_proxy.invoke.return_value = "remote"

        ray_method_proxy = ray_adapter.actor.ActorMethod(mock_method_proxy)
        res = ray_method_proxy.remote(1)
        self.assertEqual(res, "remote")

    @patch.object(yr.InstanceCreator, '_invoke')
    def test_ray_instance_creator(self, mock_invoke):
        class NoBody:
            pass
        mock_invoke.return_value = yr.InstanceProxy.__new__(yr.InstanceProxy)
        ray_instance_creator = ray_adapter.actor.ActorClass._ray_from_modified_class(NoBody, None)
        res = ray_instance_creator.remote(1)
        self.assertIsInstance(res, ray_adapter.actor.ActorHandle)

    def test_build_yr_scheduling_options(self):
        res = build_yr_scheduling_options(name="name", namespace="namespace",
                                          scheduling_strategy=ray_adapter.util.NodeAffinitySchedulingStrategy(
                                              node_id="node_id", soft=True))
        self.assertEqual(len(res.schedule_affinities), 1)
        res = build_yr_scheduling_options(name="name", namespace="namespace",
                                          scheduling_strategy=ray_adapter.util.PlacementGroupSchedulingStrategy(
                                              placement_group=None, placement_group_bundle_index=0))
        self.assertEqual(res.resource_group_options.bundle_index, 0)


if __name__ == "__main__":
    unittest.main()
