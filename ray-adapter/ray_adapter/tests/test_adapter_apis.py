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
import unittest
from unittest.mock import patch, Mock

import ray_adapter.runtime_context as runtime_context
import yr
import ray_adapter.worker as worker
from yr.config_manager import ConfigManager
from ray_adapter.worker import remote, method
from ray_adapter.actor import RemoteFunction, ActorClass

_runtime_ctx_instance = runtime_context.get_runtime_context()
runtime_context.get_accelerator_ids = _runtime_ctx_instance.get_accelerator_ids
runtime_context.get_node_id = _runtime_ctx_instance.get_node_id
runtime_context.namespace = _runtime_ctx_instance.namespace


class TestIsInitialized(unittest.TestCase):

    @patch('yr.apis.is_initialized')
    def test_is_initialized_true(self, mock_is_initialized):
        mock_is_initialized.return_value = True
        self.assertTrue(worker.is_initialized())

    @patch('yr.apis.is_initialized')
    def test_is_initialized_false(self, mock_is_initialized):
        mock_is_initialized.return_value = False
        self.assertFalse(worker.is_initialized())

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_shutdown(self, get_runtime):
        mock_runtime = Mock()
        mock_runtime.finalize.return_value = None
        mock_runtime.receive_request_loop.return_value = None
        mock_runtime.exit.return_value = None
        get_runtime.return_value = mock_runtime

        # mock ConfigManager().meta_config
        ConfigManager().meta_config = Mock()
        ConfigManager().meta_config.jobID = "test-job-id"

        yr.apis.set_initialized()
        self.assertTrue(worker.is_initialized())
        worker.shutdown()
        self.assertFalse(worker.is_initialized())

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_available_resources_per_node_success(self, mock_get_runtime):
        mock_runtime = Mock()
        mock_runtime.resources.return_value = [
            {"id": "node1", "allocatable": {"cpu": "2", "memory": "2Gi"}},
            {"id": "node2", "allocatable": {"cpu": "4", "memory": "4Gi"}},
        ]
        mock_get_runtime.return_value = mock_runtime

        yr.apis.set_initialized()
        result = worker.available_resources_per_node()

        self.assertEqual(result, {
            "node1": {"cpu": "2", "memory": "2Gi"},
            "node2": {"cpu": "4", "memory": "4Gi"},
        })


class TestKill(unittest.TestCase):

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_kill_with_non_actor(self, _):
        non_actor = "I am not an actor"
        yr.apis.set_initialized()
        with self.assertRaises(ValueError):
            worker.kill(non_actor)

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_kill_with_actor_no_terminate(self, _):
        actor = Mock()
        del actor.terminate
        yr.apis.set_initialized()
        with self.assertRaises(ValueError):
            worker.kill(actor)

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_kill_with_actor_terminate_not_callable(self, _):
        actor = Mock()
        actor.terminate = "I am not callable"
        yr.apis.set_initialized()
        with self.assertRaises(ValueError):
            worker.kill(actor)

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_kill_with_actor_terminate_success(self, _):
        actor = Mock()
        actor.terminate.return_value = True
        yr.apis.set_initialized()
        self.assertTrue(worker.kill(actor))
        actor.terminate.assert_called_once_with(is_sync=True)

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_kill_with_actor_terminate_exception(self, _):
        actor = Mock()
        actor.terminate.side_effect = Exception("terminate failed")
        yr.apis.set_initialized()
        with self.assertLogs(level='WARNING') as log:
            self.assertIsNone(worker.kill(actor))
        self.assertIn("Failed to kill actor", log.output[0])
        actor.terminate.assert_called_once_with(is_sync=True)


def sample_func(x):
    return x + 1


def my_func():
    return "hello"


class TestMethodAdaptor(unittest.TestCase):

    @patch("logging.warning")
    def test_method_decorate_function(self, mock_warning):
        result = worker.method(sample_func)
        self.assertEqual(result, sample_func)
        mock_warning.assert_called_once()
        args, kwargs = mock_warning.call_args
        self.assertIn("method only supports member methods", args[0])

    @patch("yr.apis.method")
    def test_method_call_underlying(self, mock_apis_method):
        mock_apis_method.return_value = "ok"
        result = worker.method("class_instance", 1, 2, key="value")
        mock_apis_method.assert_called_once_with("class_instance", 1, 2, key="value")
        self.assertEqual(result, "ok")

    @patch("logging.warning")
    def test_method_decorator_warning(self, mock_warning):
        result = worker.method(my_func)
        self.assertIs(result, my_func)

        mock_warning.assert_called_once()
        called_args = mock_warning.call_args[0]
        self.assertIn(
            "method only supports member methods in this runtime", str(called_args)
        )


class TestInit(unittest.TestCase):

    @patch('yr.init')
    def test_init_with_default_values(self, mock_yr_init):
        worker.init()
        conf_arg = mock_yr_init.call_args[0][0]
        self.assertEqual(conf_arg.log_level, "WARNING")

    @patch('yr.init')
    def test_init_with_custom_logging_level(self, mock_yr_init):
        worker.init(logging_level=40)
        conf_arg = mock_yr_init.call_args[0][0]
        self.assertEqual(conf_arg.log_level, "ERROR")

    @patch('yr.init')
    def test_init_with_custom_num_cpus(self, mock_yr_init):
        worker.init(num_cpus=4)
        conf_arg = mock_yr_init.call_args[0][0]
        self.assertEqual(conf_arg.num_cpus, 4)

    @patch('yr.init')
    def test_init_with_runtime_env(self, mock_yr_init):
        env = {'key': 'value'}
        worker.init(runtime_env=env)
        conf_arg = mock_yr_init.call_args[0][0]
        self.assertEqual(conf_arg.runtime_env, env)


class TestHelpers(unittest.TestCase):
    @patch.dict(os.environ, {
        "YR_NOSET_ASCEND_RT_VISIBLE_DEVICES": "1",
        "NPU_DEVICE_IDS": "0,1"
    }, clear=True)
    def test_get_accelerator_ids_npu_device_ids(self):
        acc_ids = runtime_context.get_accelerator_ids()
        self.assertIn("npu", acc_ids)
        self.assertEqual(acc_ids["npu"], ["0", "1"])

    @patch.dict(os.environ, {
        "ASCEND_RT_VISIBLE_DEVICES": "2,3"
    }, clear=True)
    def test_get_accelerator_ids_ascend_visible(self):
        acc_ids = runtime_context.get_accelerator_ids()
        self.assertIn("npu", acc_ids)
        self.assertEqual(acc_ids["npu"], ["2", "3"])

    @patch.dict(os.environ, {
        "DEVICE_INFO": "gpu0,gpu1"
    }, clear=True)
    def test_get_accelerator_ids_device_info(self):
        acc_ids = runtime_context.get_accelerator_ids()
        self.assertIn("device", acc_ids)
        self.assertEqual(acc_ids["device"], ["gpu0", "gpu1"])

    @patch("yr.runtime_holder.global_runtime.get_runtime")
    def test_get_node_id(self, mock_get_runtime):
        mock_runtime = Mock()
        mock_runtime.get_node_id.return_value = "node_123"
        mock_get_runtime.return_value = mock_runtime

        yr.apis.set_initialized()
        result = runtime_context.get_node_id()
        self.assertEqual(result, "node_123")

    def test_namespace(self):
        result = runtime_context.namespace
        self.assertEqual(result, "", f"Expected empty string, got {repr(result)}")
        self.assertIsInstance(result, str)


class TestRemote(unittest.TestCase):
    def test_remote_with_invalid_args(self):
        with self.assertRaises(TypeError):
            @remote(max_retries="invalid")
            def test_function():
                pass
        with self.assertRaises(ValueError):
            @remote(max_retries=-1)
            def test_function():
                pass
        with self.assertRaises(TypeError):
            @remote(num_cpus="invalid")
            def test_function():
                pass

    def test_remote_with_callable(self):
        @remote
        def test_function():
            pass

        self.assertIsInstance(test_function, RemoteFunction)
        inner_proxy = test_function._RemoteFunction__function_proxy
        self.assertTrue(hasattr(inner_proxy, "invoke_options"))
        self.assertEqual(inner_proxy.invoke_options.concurrency, 1)
        self.assertEqual(inner_proxy.invoke_options.custom_resources, {})

    def test_remote_with_custom_resources_and_concurrency(self):
        @remote(resources={"NPU": 0.7}, concurrency_groups={"acquire": 1, "release": 10})
        def test_function():
            pass

        self.assertIsInstance(test_function, RemoteFunction)
        inner_proxy = test_function._RemoteFunction__function_proxy
        self.assertTrue(hasattr(inner_proxy, "invoke_options"))
        self.assertEqual(inner_proxy.invoke_options.custom_resources, {"npu": 0.7})
        self.assertEqual(inner_proxy.invoke_options.concurrency, 11)

    def test_remote_num_npus_boundaries(self):
        with self.assertRaises(ValueError):
            @remote(num_gpus=0)
            def fn_zero():
                pass
        with self.assertRaises(ValueError):
            @remote(num_gpus=-1)
            def fn_negative():
                pass

        @remote(num_gpus=0.001)
        def fn_small():
            pass

        inner_proxy = fn_small._RemoteFunction__function_proxy
        self.assertEqual(inner_proxy.invoke_options.custom_resources.get("GPU"), 0.001)

    def test_remote_num_cpus(self):
        with self.assertRaises(TypeError):
            @remote(num_cpus=-1)
            def func():
                pass
        with self.assertRaises(TypeError):
            @remote(num_cpus='x')
            def fn():
                pass

        @remote(num_cpus=2.2)
        def cpu_float():
            pass

        inner_proxy = cpu_float._RemoteFunction__function_proxy
        self.assertEqual(inner_proxy.invoke_options.cpu, 2200.0)


if __name__ == "__main__":
    unittest.main()
