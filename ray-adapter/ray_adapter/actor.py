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
import copy

from yr.affinity import AffinityType, AffinityKind, OperatorType, LabelOperator, Affinity
from yr.config import InvokeOptions, ResourceGroupOptions
from yr.decorator.function_proxy import FunctionProxy
from yr.decorator.instance_proxy import InstanceCreator, MethodProxy, InstanceProxy
from ray_adapter.util.scheduling_strategies import PlacementGroupSchedulingStrategy, NodeAffinitySchedulingStrategy


def build_yr_scheduling_options(opts, *args, **kwargs) -> InvokeOptions:
    """build yr scheduling options"""
    scheduling_strategy = kwargs.get("scheduling_strategy", None)
    if isinstance(scheduling_strategy, PlacementGroupSchedulingStrategy):
        rg_opts = ResourceGroupOptions()
        if scheduling_strategy.placement_group is not None:
            rg_opts.resource_group_name = scheduling_strategy.placement_group.resource_group.name
        rg_opts.bundle_index = scheduling_strategy.placement_group_bundle_index
        opts.resource_group_options = rg_opts
    if isinstance(scheduling_strategy, NodeAffinitySchedulingStrategy):
        operators = [LabelOperator(OperatorType.LABEL_IN, "NODE_ID", [scheduling_strategy.node_id])]
        if scheduling_strategy.soft:
            opts.schedule_affinities = [Affinity(AffinityKind.RESOURCE, AffinityType.PREFERRED_ANTI, operators)]
        else:
            opts.schedule_affinities = [Affinity(AffinityKind.RESOURCE, AffinityType.REQUIRED, operators)]

    lifecycle = kwargs.get("lifetime")
    if lifecycle is not None:
        if not isinstance(lifecycle, str):
            raise TypeError(
                f"lifetime must be None or a string, got: '{type(lifecycle)}'.")
        if lifecycle != "detached":
            raise ValueError(f"lifetime is only support detached")
        opts.custom_extensions["lifecycle"] = lifecycle

    num_cpus = kwargs.get("num_cpus")
    if num_cpus is not None:
        if not isinstance(num_cpus, int):
            raise TypeError("num_cpus must be an integer")
        opts.cpu = int(num_cpus * 1000) if num_cpus >= 0 else 1000

    num_gpus = kwargs.get("num_gpus")
    if num_gpus is not None:
        if not isinstance(num_cpus, int):
            raise TypeError("num_gpus must be an integer")
        if num_gpus < 0.0001:
            raise ValueError("Parameter 'num_gpus' cannot be set to < 0.0001")
        opts.custom_resources["GPU/.+/count"] = float(num_gpus)

    max_concurrency = kwargs.get("max_concurrency")
    if max_concurrency is not None:
        if not isinstance(max_concurrency, int):
            raise TypeError("max_concurrency must be an integer")
        opts.concurrency = int(max_concurrency)
    custom_resources = kwargs.get("resources", {})
    for k, v in custom_resources.items():
        if v is None:
            raise ValueError("resources is not a valid resources")
        if "memory" in k:
            opts.memory = v
        elif "NPU" in k:
            opts.custom_resources["NPU/.+/count"] = v
        else:
            opts.custom_resources[k] = v
    runtime_env = kwargs.get("runtime_env")
    if runtime_env is not None:
        opts.runtime_env = runtime_env
    return opts


class RemoteFunction:
    def __init__(self, function_proxy: FunctionProxy):
        self.__function_proxy = function_proxy
        self.__opts_wrapper = None

    def options(self, *args, **kwargs):
        """options"""
        opt = copy.deepcopy(self.__function_proxy.invoke_options)
        opts = build_yr_scheduling_options(opt, *args, **kwargs)
        self.__opts_wrapper = self.__function_proxy.create_opts_wrapper(opts)
        return self

    def remote(self, *args, **kwargs):
        """remote"""
        if self.__opts_wrapper is None:
            return self.__function_proxy.invoke(*args, **kwargs)
        return self.__opts_wrapper.invoke(*args, **kwargs)


class ActorMethod:
    def __init__(self, method_proxy: MethodProxy):
        self.__method_proxy = method_proxy

    def remote(self, *args, **kwargs):
        """remote"""
        return self.__method_proxy.invoke(*args, **kwargs)


class ActorHandle:
    def __init__(self, instance_proxy: InstanceProxy):
        self.__instance_proxy = instance_proxy

    def __getattr__(self, method_name):
        return ActorMethod(getattr(self.__instance_proxy, method_name))

    def __getstate__(self):
        attrs = self.__dict__.copy()
        return attrs

    def __setstate__(self, state):
        self.__dict__.update(state)

    def terminate(self, is_sync: bool = False):
        """terminate actor"""
        return self.__instance_proxy.terminate(is_sync)


class ActorClass:
    def __init__(self):
        self.__instance_creator = None
        self.__option_wrapper = None

    @classmethod
    def _ray_from_modified_class(
            cls,
            modified_class,
            actor_options,
    ):
        for attribute in [
            "remote",
            "_remote",
            "_ray_from_modified_class",
            "_ray_from_function_descriptor",
        ]:
            if hasattr(modified_class, attribute):
                logger.warning(
                    "Creating an actor from class "
                    f"{modified_class.__name__} overwrites "
                    f"attribute {attribute} of that class"
                )

        # Make sure the actor class we are constructing inherits from the
        # original class so it retains all class properties.
        class DerivedActorClass(cls, modified_class):
            def __init__(self, *args, **kwargs):
                try:
                    cls.__init__(self, *args, **kwargs)
                except Exception as e:
                    # Delegate call to modified_class.__init__ only
                    # if the exception raised by cls.__init__ is
                    # TypeError and not ActorClassInheritanceException(TypeError).
                    # In all other cases proceed with raise e.
                    if isinstance(e, TypeError):
                        modified_class.__init__(self, *args, **kwargs)
                    else:
                        raise e

        name = f"ActorClass({modified_class.__name__})"
        DerivedActorClass.__module__ = modified_class.__module__
        DerivedActorClass.__name__ = name
        DerivedActorClass.__qualname__ = name
        # Construct the base object.
        self = DerivedActorClass.__new__(DerivedActorClass)
        self.__instance_creator = InstanceCreator.create_from_user_class(modified_class, actor_options)
        self.__option_wrapper = None
        return self

    def options(self, *args, **kwargs):
        """options"""
        name = kwargs.get("name")
        namespace = kwargs.get("namespace")
        if name is not None:
            if not isinstance(name, str):
                raise TypeError(
                    f"name must be None or a string, got: '{type(name)}'.")
            if name == "":
                raise ValueError("stateful function name cannot be an empty string.")
        if namespace is not None:
            if not isinstance(namespace, str):
                raise TypeError("namespace must be None or a string.")
            if namespace == "":
                raise ValueError('"" is not a valid namespace. '
                                 "Pass None to not specify a namespace.")
        opt = build_yr_scheduling_options(self.__instance_creator.__invoke_options__, *args, **kwargs)
        opt.name = name
        opt.namespace = namespace
        self.__option_wrapper = self.__instance_creator.options(opt)
        return self

    def remote(self, *args, **kwargs) -> ActorHandle:
        """remote"""
        if self.__option_wrapper is None:
            return ActorHandle(self.__instance_creator.invoke(*args, **kwargs))
        return ActorHandle(self.__option_wrapper.invoke(*args, **kwargs))
