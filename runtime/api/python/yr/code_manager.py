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

"""code manager"""

import importlib.util
import os
import sys
import threading
from typing import Callable, List

from yr import log
from yr.common import constants
from yr.common.singleton import Singleton
from yr.err_type import ErrorInfo
from yr.libruntime_pb2 import LanguageType

_DEFAULT_ADMIN_FUNC_PATH = "/adminfunc/"


@Singleton
class CodeManager:
    """
    CodeManager provides code loading service
    it will try to cache the code and load from the cache as possible
    so it will only load
    * one module at most once (identified by the module file path)
    * one function at most once (identified by the module and function name)
    """

    def __init__(self):
        # code_key : code
        self.code_map = {}
        # code_key: code_path
        self.entry_map = {}
        self.module_cache = {}
        self.__lock = threading.Lock()
        self.custom_handler = os.environ.get(constants.ENV_KEY_ENV_DELEGATE_DOWNLOAD)
        self.deploy_dir = os.environ.get(constants.ENV_KEY_FUNCTION_LIBRARY_PATH)
        self.load_code_from_datasystem_func: Callable = None

    @staticmethod
    def load_functions(code_paths: List[str]) -> ErrorInfo:
        """
        Load code paths and return ErrorInfo object.

        Args:
            code_paths (List[str]): List of paths to load.

        Returns:
            ErrorInfo: Error information for any errors encountered during loading.
        """
        for code_path in code_paths:
            sys.path.insert(constants.INDEX_FIRST, code_path)
        return ErrorInfo()

    def clear(self):
        """clear"""
        self.code_map.clear()
        self.entry_map.clear()
        self.module_cache.clear()

    def register_load_code_from_datasystem_func(self, function: Callable):
        """
        register load code from datasystem
        """
        self.load_code_from_datasystem_func = function

    def register(self, function_key: str, function_obj: Callable):
        """
        register function code to code manager
        """
        with self.__lock:
            self.code_map[function_key] = function_obj

    def load(self, function_key):
        """
        load function code
        """
        with self.__lock:
            if function_key not in self.code_map:
                log.get_logger().info("Code has not been registered in cache, code key: %s", function_key)
                return
            code = self.code_map.get(function_key)

        if code is None:
            # Although the 'register' method has raised an error if code is None,
            # the 'register' method and the 'load' method are not in the same thread,
            # the 'load' method will be executed even though the 'register' raise error.
            log.get_logger().error("Failed to load code from local cache, code is None. Code key: %s", function_key)
            return

        log.get_logger().debug("Succeeded to load code. Code key: %s", function_key)
        return code

    def get_code_path(self, code_key):
        """
        get code path
        """
        with self.__lock:
            return self.entry_map.get(code_key, None)

    def load_code(self, func_meta, is_class=False):
        """
        load user code
        """
        if len(func_meta.codeID) != 0:
            return self.load_code_from_datasystem(func_meta.codeID)
        log.get_logger().debug(f'there is no code id in meta, try to load function from local {func_meta}')
        code_name = func_meta.className if is_class else func_meta.functionName
        code = self.load_code_from_local(self.deploy_dir, func_meta.moduleName, code_name)
        log.get_logger().debug(f'finished load function from local {self.deploy_dir}, module name '
                               f'{func_meta.moduleName}, code name {code_name}')
        return code

    def load_code_from_datasystem(self, code_id: str, language: LanguageType = LanguageType.Python):
        """
        load code from datasystem
        """
        if language != LanguageType.Python:
            # add cross language
            log.get_logger().error("Intermodulation of functions in different languages is not supported.")

        code = self.load(code_id)
        if code is not None:
            return code

        if self.load_code_from_datasystem_func is None:
            msg = (
                f"Failed to load code from data system, code ID: [{code_id}]."
                "Function for getting code from data system has not been not registered."
            )
            log.get_logger().error(msg)
            raise RuntimeError(msg)
        code = self.load_code_from_datasystem_func(code_id)
        if code is None:
            msg = f"Failed to import function from datasystem by {code_id}"
            log.get_logger().error(msg)
            raise ImportError(msg)

        log.get_logger().debug("Succeeded to load code from datasystem. Code key: %s", code_id)
        self.register(code_id, code)
        return code

    def load_code_from_local(self, code_dir, module_name, entry_name, code_key: str = ""):
        """
        get code from local,
        throw error if module not exists
        return None if module exists but entry not exists
        """
        log.get_logger().debug("get python code [%s] from local file [%s/%s.py]",
                               entry_name, code_dir, module_name)
        code_key = module_name + "%%" + entry_name if code_key == "" else code_key
        code = self.load(code_key)
        if code is not None:
            return code

        module = self.__load_module(code_dir, module_name)
        code = getattr(module, entry_name, None)
        if code is None:
            msg = f"Failed to import function {entry_name} from {module_name}"
            log.get_logger().error(msg)
            raise ImportError(msg)
        self.register(code_key, code)
        log.get_logger().debug("Succeeded to load code from file %s/%s.py. code_key: %s, entry_name: %s",
                               code_dir, module_name, code_key, entry_name)
        return code

    def __load_module(self, code_dir, module_name):
        """load module using cache"""
        if code_dir is None:
            log.get_logger().debug(
                "code dir is None, import from module name %s directly.", module_name)
            module = self.module_cache.get(module_name, None)
            if module is not None:
                return module
            module = importlib.import_module(module_name)
            self.module_cache[module_name] = module
            return module

        file_path = os.path.join(code_dir, module_name + ".py")
        if not os.path.exists(file_path):
            admin_path = os.path.join(_DEFAULT_ADMIN_FUNC_PATH, module_name + ".py")
            if not os.path.exists(admin_path):
                raise ValueError("entry file does not exist: [{}]".format(file_path))
            file_path = admin_path

        module = self.module_cache.get(file_path, None)
        if module is not None:
            log.get_logger().debug("successfully load module [%s] from cache", file_path)
            return module

        log.get_logger().debug("loading module [%s] from file system", file_path)
        module_spec = importlib.util.spec_from_file_location(module_name, file_path)
        try:
            module = importlib.util.module_from_spec(module_spec)
        except ImportError as exp:
            log.get_logger().warning("failed to import user python module, %s", str(exp))
            raise

        try:
            # In case of user code errors, the exec_module operation does not throw errors
            # in non-main threads, user code would be None when execute.
            module_spec.loader.exec_module(module)
        except (ImportError, SyntaxError) as exp:
            log.get_logger().warning("Failed to exec_module user module, please check user code. %s", exp)
            raise
        self.module_cache[file_path] = module
        return module
