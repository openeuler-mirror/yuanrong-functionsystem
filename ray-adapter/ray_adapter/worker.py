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
import inspect
import functools
import logging
from collections import defaultdict
from typing import Optional, List, Dict, Union, Any, Sequence, Tuple

import yr.apis
from yr.decorator.function_proxy import FunctionProxy
from yr.decorator.instance_proxy import InstanceCreator, InstanceProxy
from yr.apis import get_instance, resources
from yr.object_ref import ObjectRef
from yr.config import InvokeOptions
from yr.common import constants

from yr.config import Config
from ray_adapter.actor import ActorClass, RemoteFunction, ActorHandle


def is_cython(obj):
    """Check if an object is a Cython function or method"""

    def check_cython(x):
        return type(x).__name__ == "cython_function_or_method"

    # Check if function or method, respectively
    return check_cython(obj) or (
        hasattr(obj, "__func__") and check_cython(obj.__func__)
    )


def is_function_or_method(obj):
    """
    Check if an object is a function or method.

    Args:
        obj: The Python object in question.

    Returns:
        True if the object is an function or method.
    """
    return inspect.isfunction(obj) or inspect.ismethod(obj) or is_cython(obj)


def _modify_class(cls):
    # cls has been modified.
    if hasattr(cls, "__ray_actor_class__"):
        return cls

    # Modify the class to have additional default methods.
    class Class(cls):
        __ray_actor_class__ = cls  # The original actor class

        def __ray_ready__(self):
            return True

        def __ray_call__(self, fn, *args, **kwargs):
            return fn(self, *args, **kwargs)

        def __ray_terminate__(self):
            worker = ray._private.worker.global_worker
            if worker.mode != ray.LOCAL_MODE:
                ray.actor.exit_actor()

    Class.__module__ = cls.__module__
    Class.__name__ = cls.__name__

    if not is_function_or_method(getattr(Class, "__init__", None)):
        # Add __init__ if it does not exist.
        # Actor creation will be executed with __init__ together.

        # Assign an __init__ function will avoid many checks later on.
        def __init__(self):
            pass

        Class.__init__ = __init__

    return Class


def _make_remote(function_or_class, options):
    """
    Based on the passed parameters, determine whether it is a function or a class.
    Create a remote task according to different types.

    Args:
         function_or_class (Union[Callable, Type]): The passed class or object.
         options (dict): The passed configuration parameters.

    Returns:
        If `function_or_class` is a function, return a FunctionProxy object.
        If `function_or_class` is a class, return a created instance.

    Raises:
        ValueError: The number of `max_retries` is negative.
        TypeError: isinstance (num_cpus, int): Check whether `num_cpus` is of type int, otherwise, throw an exception.
        TypeError: isinstance (max_retries, int): Check whether `max_retries` is of int type,
            otherwise, throw an exception.
        TypeError: `nums_cpus` <0: If the number of `nums_cpus` is negative or not of int type, an exception is thrown.
            If `nums_cpus` =0, the default assignment is 300 millikerbs.
        TypeError: max_retries: Less than 0 is not supported.
        TypeError: `num_gpus` is not of int type or float type, an exception is thrown.
    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> def test_function():
        ...     return a+b
        >>>remote_test_function=ray._make_remote(test_function,{"num_cpus":2,"max_retries":3})
        >>>obj_ref_1=remote_test_function.remote(1,2)
        >>> obj_ref_2=remote_test_function.remote(3,4)
        >>> result=ray.get(obj_ref_1,obj_ref_2,timeout=-1)
        >>> print(result)
        >>> ray.shutdown()
    """
    opts = InvokeOptions()

    lifecycle = options.get("lifetime")
    if lifecycle is not None:
        if not isinstance(lifecycle, str):
            raise TypeError(
                f"lifetime must be None or a string, got: '{type(lifecycle)}'.")
        if lifecycle != "detached":
            raise ValueError(f"lifetime is only support detached")
        options.custom_extensions["lifecycle"] = lifecycle

    num_cpus = options.get("num_cpus", 1)
    if not isinstance(num_cpus, (int, float)):
        raise TypeError("Parameter 'num_cpus' must be a number.")
    if num_cpus < 0:
        raise TypeError("Parameter 'num_cpus' cannot be set to < 0.")
    opts.cpu = num_cpus * 1000
    opts.memory = 0

    max_retries = options.get("max_retries", 3)
    if not isinstance(max_retries, int):
        raise TypeError("Parameter 'max_retries' must be an integer.")
    if max_retries < 0:
        raise ValueError("Parameter 'max_retries' cannot be set to < 0.")
    opts.retry_time = max_retries

    max_concurrency = options.get("max_concurrency", 1)
    if not isinstance(max_concurrency, int):
        raise TypeError("Parameter 'max_concurrency' must be an integer.")
    opts.concurrency = max_concurrency

    concurrency_groups = options.get("concurrency_groups", None)
    if concurrency_groups is not None:
        if not isinstance(concurrency_groups, dict):
            raise TypeError("Parameter 'concurrency_groups' must be a dict if provided.")
        valid_values = [v for v in concurrency_groups.values() if v is not None]
        if not valid_values:
            raise ValueError("All keys in concurrency_groups have None values.")
        concurrency_sum = sum(valid_values)
        if not isinstance(concurrency_sum, int):
            raise ValueError("The sum of concurrency_groups values must be an integer.")
        opts.concurrency = sum(valid_values) + 1

    custom_resources: Dict[str, float] = {}
    num_gpus = options.get("num_gpus")
    if num_gpus is not None:
        if not isinstance(num_gpus, (int, float)):
            raise TypeError("Parameter 'num_gpus' must be a number.")
        if num_gpus < 0.0001:
            raise ValueError("Parameter 'num_gpus' cannot be set to < 0.0001")
        custom_resources["GPU/.+/count"] = float(num_gpus)
    if "resources" in options and isinstance(options["resources"], dict):
        if "NPU" in options["resources"]:
            nums_npu = options["resources"].get("NPU")
            if not isinstance(nums_npu, (int, float)):
                raise TypeError("Parameter 'nums_npu' must be a number.")
            if nums_npu < 0.0001:
                raise ValueError("Parameter 'nums_npu' cannot be set to < 0.0001")
            custom_resources["NPU/.+/count"] = float(nums_npu)
            options["resources"].pop("NPU")
        if "memory" in options["resources"]:
            nums_memory = options["resources"].get("memory")
            if not isinstance(nums_memory, (int, float)):
                raise TypeError("Parameter 'nums_npu' must be a number.")
            if nums_memory < 0.0001:
                raise ValueError("Parameter 'nums_npu' cannot be set to < 0.0001")
            opts.memory = nums_memory
            options["resources"].pop("memory")
        custom_resources.update(options["resources"])
    opts.custom_resources = custom_resources

    runtime_env = options.get("runtime_env")
    if runtime_env is not None:
        opts.runtime_env = runtime_env

    if inspect.isfunction(function_or_class):
        return_nums = options.get("num_returns", None)
        return RemoteFunction(
            FunctionProxy(function_or_class, invoke_options=opts, return_nums=return_nums, initializer=None))
    if inspect.isclass(function_or_class):
        return ActorClass._ray_from_modified_class(_modify_class(function_or_class), opts)
    raise TypeError(
        "The remote decorator must be applied to either a function or a class."
    )


def remote(*args, **kwargs) -> Union[RemoteFunction, ActorClass]:
    """
    Define a remote function or a participating class. This function can be defined as a decorator without parameters.

    Args:
        *args (Union[Callable, type]): For objective functions or classes,
            you can pass an objective function or class to a decorator through `*args` to wrap a remote call.
        **kwargs (Dict[str, Any]): Any named parameters (`num_cpus`: the number of cpus used by the specified task,
            `max_retries`: the maximum number of retries,resources: the required custom resources).

    Returns:
        A remote function configured with kwargs.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> @ray.remote(num_cpus=1, max_retries=3)
        >>> def test_function():
        ...    return "Hello!"
        >>> remote_function = test_function.remote()
        >>> result = ray.get(remote_function)
        >>> print(result)
        >>> ray.finalize()
    """
    if len(args) == 1 and not kwargs and callable(args[0]):
        return _make_remote(args[0], {})

    return functools.partial(_make_remote, options=kwargs)


def get_actor(name: str, namespace: Optional[str] = None) -> ActorHandle:
    """
    Obtain the actor with the specified name.

    Args:
        name (str): The `name` of the actor to be obtained.
        namespace (str, optional): The namespace of an actor, if it is None, defaults to the current `namespace`.

    Returns:
        Calling `get_instance()` returns a Python named instance handle.

    Raises:
        ValueError: If the name of the actor does not exist.
        TypeError: If the passed namespace is not of str type.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>>actor=ray.get_actor("test_actor")
        >>> print(actor)
        >>> ray.shutdown()
    """
    if not name:
        raise ValueError("Please provide a valid name for the actor.")

    if namespace is not None:
        if not isinstance(namespace, str):
            raise TypeError("namespace must be None or a string.")
        if namespace == "":
            raise ValueError('"" is not a valid namespace. '
                             "Pass None to not specify a namespace.")
    return ActorHandle(get_instance(name, namespace or ""))


def nodes() -> List[Dict]:
    """
    Get node information in the cluster.

    Returns:
        A list of node_infos.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> node_info=ray.nodes()
        >>>for node in node_info:
        >>> print(node)
        >>> ray.shutdown()
    """
    yr_resources = resources()
    node_infos = []
    for node in yr_resources:
        res_dict = {}
        ray_node_info = {
            "NodeID": node.get("id", ""),
            "Alive": node.get("status", -1) == 0,
        }
        for key, value in node["capacity"].items():
            if "NPU" in key:
                res_dict["NPU"] = res_dict.get("NPU", 0) + value
            elif "Memory" in key:
                res_dict["memory"] = value
            else:
                res_dict[key] = value
        ray_node_info["Resources"] = res_dict
        ray_node_info["labels"] = node.get("labels", {}).copy()
        node_infos.append(ray_node_info)
    return node_infos


def available_resources() -> Dict:
    """
    Obtain the currently available cluster nodes, and convert defaultdict to a regular dictionary.

    Returns:
        The converted regular dictionary. Data type is Dict.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> resources=ray.available_resources()
        >>> print(resources)
        >>> ray.shutdown()
    """
    yr_resources = resources()
    available_dict = defaultdict(float)
    for node in yr_resources:
        for key, value in node["allocatable"].items():
            if abs(value) < 1e-6:
                continue
            if "NPU" in key:
                available_dict["NPU"] += value
            elif "Memory" in key:
                available_dict["memory"] += value
            else:
                available_dict[key] += value
    return dict(available_dict)


def cluster_resources() -> Dict:
    """
    Obtain the currently available cluster nodes, and convert defaultdict to a regular dictionary.

    Returns:
        The converted regular dictionary. Data type is Dict.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> cluster=ray.cluster_resources()
        >>> print(cluster)
        >>> ray.shutdown()
    """
    yr_resources = resources()
    total_resources = defaultdict(float)
    for node in yr_resources:
        for key, value in node["capacity"].items():
            if "NPU" in key:
                total_resources["NPU"] += value
            if "Memory" in key:
                total_resources["memory"] += value
            else:
                total_resources[key] += value
    return dict(total_resources)


def get(ray_waitables: Union[
    "ObjectRef[Any]",
    Sequence["ObjectRef[Any]"],
],
        *,
        timeout: Optional[float] = None,
) -> Union[Any, List[Any]]:
    """
    Obtain one or more objects from object store.

    Args:
        ray_waitables (Union[ObjectRef[Any], Sequence[ObjectRef[Any]]]): The list of `object` references to be obtained.
        timeout (Optional[float], optional): Passing < 0 and None indicates the default `timeout`.

    Returns:
       Input a single object and return an object.
       Input multiple objects and return the list of objects.

    Raises:
        ValueError: If timeout==0 is passed in, the object is not returned immediately.

    Examples:
        >>> import ray_adapter as ray
        >>>ray.init()
        >>>@ray.remote()
        >>> def add(a,b):
        ...     return a+b
        >>> obj_ref_1=add.remote(1,2)
        >>> obj_red_2=add.remote(3,4)
        >>> result=ray.get([obj_ref_1,obj_ref_2],timeout=-1)
        >>> print(result)
        >>> ray.shutdown()
    """
    if timeout is None or timeout < 0:
        timeout = constants.NO_LIMIT
        yr_get = yr.apis.get(ray_waitables, timeout)
    else:
        try:
            yr_get = yr.apis.get(ray_waitables, int(timeout))
        except ValueError as e:
            raise ValueError("No object returned when time is 0") from e
    return yr_get


def is_initialized() -> bool:
    """
    Check if init has been called yet.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()

    """

    return yr.apis.is_initialized()


def shutdown() -> None:
    """
    Shut down and clean up the runtime environment.

    This function calls yr.apis.finalize() to perform necessary cleanup operations.

    Returns:
        None

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> ray.shutdown()
    """
    return yr.apis.finalize()


def available_resources_per_node():
    """
    Return available resources grouped by node .
    """
    yr_resources = yr.apis.resources()

    result = {}
    for node in yr_resources:
        node_name = node.get("id")
        allocatable = node.get("allocatable", {})
        processd_alloc = {}
        for key, value in allocatable.items():
            if key == "Memory":
                r_key = "memory"
            elif key.startswith("NPU"):
                r_key = "NPU"
            elif key.startswith("GPU"):
                r_key = "GPU"
            else:
                r_key = key
            processd_alloc[r_key] = value
        result[node_name] = processd_alloc
    return result


def kill(actor):
    """
    Kill an actor forcefully.

    Args:
        actor: The actor instance to be terminated.

    Raises:
        ValueError: If the provided actor is not an instance of ActorHandle.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> @ray.remote
        ... class Actor:
        ...     def fun(self):
        ...         return 2
        >>> a = Actor.remote()
        >>> ray.kill(a)

    """
    if not isinstance(actor, ActorHandle):
        raise ValueError(
            f"kill() only supported for ActorHandle, actual {type(actor)}"
        )
    try:
        return actor.terminate(is_sync=True)
    except Exception as e:
        logging.warning("Failed to kill actor %s: %s", actor, e)
        return None


def method(*args, **kwargs):
    """
    A custom decorator method used to handle the decoration of member methods.

    Parameters:
        *args: Variable-length arguments, typically used to pass functions or methods.
        **kwargs: Keyword arguments used to pass additional configuration options.

    Returns:
        The decorated method or function.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> @ray.remote()
        >>> class f():
        >>>     @ray.method(num_returns = 1)
        >>>     def method0(self):
        >>>         return 1
        >>> a = f.remote()
        >>> id = a.method0.remote()
        >>> print(ray.get(id))
        >>> ray.shutdown()

    """
    if len(args) == 1 and len(kwargs) == 0 and callable(args[0]):
        logging.warning(
            "method only supports member methods in this runtime, "
            "decorating classes or functions is not supported.",
        )
        return args[0]
    if "num_returns" in kwargs:
        kwargs["return_nums"] = kwargs.pop("num_returns")

    return yr.apis.method(*args, **kwargs)


def init(
        *,
        logging_level: int = logging.WARNING,
        num_cpus: Optional[int] = None,
        runtime_env: Optional[Dict[str, Any]] = None,
        namespace: Optional[str] = None
):
    """
    Initializes the runtime environment with the specified configuration.

    Args:
        logging_level (int): The logging level to use, defaults to WARNING.
        num_cpus (Optional[int]): The number of CPU cores available, defaults to None.
        runtime_env (Optional[Dict[str, Any]]): Configuration for the runtime environment, defaults to None.
        namespace: A namespace is a logical grouping of jobs and named actors.

    Returns:
        The result of initializing the runtime environment.

    Examples:
        >>> import ray_adapter as ray
        >>> ray.init()
        >>> ray.shutdown()
    """
    if logging_level not in [logging.DEBUG, logging.INFO, logging.WARNING, logging.ERROR]:
        raise ValueError("logging_level must be one of the logging constants")

    conf = Config()
    conf.num_cpus = num_cpus if num_cpus is not None else 0
    conf.runtime_env = runtime_env if runtime_env is not None else {}
    conf.log_level = logging.getLevelName(logging_level)
    conf.ns = namespace if namespace is not None else ""

    return yr.init(conf)


def wait(
    ray_waitables: Union[ObjectRef, List[ObjectRef]],
    wait_num: int = 1,
    timeout: Optional[int] = None,
    fetch_local: bool = True,
) -> Tuple[List[ObjectRef], List[ObjectRef]]:
    """
    Wait for the value of the object in the data system to be ready based on the object's `key`.
    The interface call will block until the value of the object is computed.

    Note:
        The results returned each time may differ because the order of completion of invoke
        is not guaranteed.

    Args:
        ray_waitables (Union[ObjectRef, List[ObjectRef]]): Data saved to the data system.
        wait_num (int, optional): The minimum number of objects to wait for. If set to ``None``, it defaults to ``1``.
            The value should not exceed the length of `ray_waitables`.
        timeout (int, optional): The timeout in seconds. Note that if the default value ``None`` is used,
            it will wait indefinitely, with the actual maximum wait time limited by the wait factors in `yr.apis.wait`.
        fetch_local (bool, optional): Whether to fetch data locally. This parameter is currently not used,
            and the actual value passed will not take effect.

    Returns:
        Tuple[List[ObjectRef], List[ObjectRef]]: A tuple containing the list of ready object references
            and the list of unready object references.
    """

    ready_ref, unready_ref = yr.apis.wait(ray_waitables, wait_num, timeout)
    return ready_ref, unready_ref
