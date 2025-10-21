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

"""executor"""

import threading
from typing import List

from yr.common.utils import get_environment_variable
from yr.err_type import ErrorInfo
from yr.executor.function_handler import FunctionHandler
from yr.executor.posix_handler import PosixHandler

HANDLER = None
_LOCK = threading.Lock()
INIT_HANDLER = "INIT_HANDLER"
ACTOR_HANDLER_MODULE_NAME = "yrlib_handler"
SERVE_HANDLER_MODULE_NAME = "serve_executor"


class Executor:
    """Execute user functions with given arguments and return the result or error.
    """

    def __init__(self, func_meta, args: List, invoke_type, return_num: int, serialization_ctx, is_actor_async: bool):
        self.func_meta = func_meta
        self.args = args
        self.invoke_type = invoke_type
        self.return_num = return_num
        self.serialization_ctx = serialization_ctx
        self.is_actor_async = is_actor_async

    @staticmethod
    def shutdown(grace_period_second: int) -> ErrorInfo:
        """
        Shutdown the instance gracefully.
        Args:
            grace_period_second (int): The time to wait for the instance to shutdown gracefully.
        Returns:
            The result of the shutdown function.
        Raises:
            RuntimeError: If the instance has not been initialized.
        """
        return HANDLER.shutdown(grace_period_second)

    @staticmethod
    def load_handler():
        """
        Determine the execution type based on the INIT_HANDLER environment variable.
        """
        init_handler = get_environment_variable("INIT_HANDLER")
        try:
            module_name, _ = init_handler.rsplit(".", 1)
        except ValueError as err:
            raise RuntimeError("Failed to parse INIT_HANDLER environment variable") from err

        if module_name == ACTOR_HANDLER_MODULE_NAME:
            handler = FunctionHandler()
        else:
            handler = PosixHandler()

        with _LOCK:
            global HANDLER
            HANDLER = handler

    def execute(self):
        """
        execute user code
        :return:
        """
        return HANDLER.execute_function(self.func_meta, self.args,
                                        self.invoke_type, self.return_num,
                                        self.is_actor_async)

    async def execute_serve(self):
        """
        execute serve user code
        :return:
        """
        return await HANDLER.execute_function(self.func_meta, self.args,
                                              self.invoke_type, self.return_num,
                                              self.is_actor_async)
