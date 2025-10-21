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

"""yr exception type"""
import cloudpickle

from yr.common import utils


class YRError(Exception):
    """
    Base class for all custom exceptions in the YR module.
    This is a base class and should not be instantiated directly.
    """


class CancelError(YRError):
    """Stateless request cancel error."""
    __slots__ = ["__task_id"]

    def __init__(self, task_id: str = ""):
        super().__init__()
        self.__task_id = task_id

    def __str__(self):
        return f"stateless request has been cancelled: {self.__task_id}"


class YRInvokeError(YRError):
    """
    Represents an error that occurred during an invocation.

    """

    def __init__(self, cause, traceback_str: str):
        self.traceback_str = traceback_str
        self.cause = cause

    def __str__(self):
        """
        Return the string representation of the exception, which is the traceback information.
        """
        return str(self.traceback_str)

    def origin_error(self):
        """
        Return a origin error for invoke stateless function.
        """
        cause_cls = self.cause.__class__
        if issubclass(YRInvokeError, cause_cls):
            return self
        if issubclass(cause_cls, YRError):
            return self

        error_msg = str(self)

        class Cls(YRInvokeError, cause_cls):
            """
            New error inherit from origin cause
            """
            def __init__(self, cause):
                self.cause = cause
                self.args = (cause,)

            def __getattr__(self, name):
                """
                Get attribute from the original cause
                """
                return getattr(self.cause, name)

            def __str__(self):
                """
                Return the error message.
                """
                return error_msg

        Cls.__name__ = f"YRInvokeError({cause_cls.__name__})"
        Cls.__qualname__ = Cls.__name__

        return Cls(self.cause)


class YRequestError(YRError, RuntimeError):
    """Request failed error."""
    __slots__ = ["__code", "__message", "__request_id"]

    def __init__(self, code: int = 0, message: str = "", request_id=""):
        self.__code = code
        self.__message = message
        self.__request_id = request_id
        super().__init__()

    def __str__(self):
        return str(f"failed to request, {self.__request_id} code: {self.__code}, message: {self.__message} ")

    @property
    def code(self) -> int:
        """code"""
        return self.__code

    @property
    def message(self) -> str:
        """message"""
        return self.__message


def deal_with_yr_error(future, err):
    """deal with yr invoke error"""
    if isinstance(err, YRInvokeError):
        future.set_exception(err.origin_error())
    else:
        future.set_exception(err)


def deal_with_error(future, code, message, task_id=""):
    """
    processing request exceptions
    """
    try:
        obj = cloudpickle.loads(utils.hex_to_binary(message))
    except (ValueError, EOFError):
        future.set_exception(YRequestError(code, message, task_id))
        return
    if isinstance(obj, YRInvokeError):
        future.set_exception(obj.origin_error())
        return
    future.set_exception(YRequestError(code, str(obj), task_id))
