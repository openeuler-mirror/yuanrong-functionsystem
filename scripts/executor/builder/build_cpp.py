# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import os.path
import platform
import shutil

import utils

log = utils.stream_logger()

DEFAULT_AARCH64_CPP_MAX_JOBS = 8


def build_gtest(root_dir, job_num):
    build_functionsystem(root_dir, job_num, build_type="Debug", gtest=True)


def build_binary(
    root_dir,
    job_num,
    version,
    *positional_options,
    **options,
):
    build_type = positional_options[0] if len(positional_options) > 0 else options.get("build_type", "Release")
    component = positional_options[1] if len(positional_options) > 1 else options.get("component", "all")
    linker = positional_options[2] if len(positional_options) > 2 else options.get("linker", "auto")
    cmake_args = positional_options[3] if len(positional_options) > 3 else options.get("cmake_args")
    build_functionsystem(
        root_dir,
        job_num,
        build_type=build_type,
        version=version,
        component=component,
        linker=linker,
        cmake_args=cmake_args,
    )


def build_functionsystem(
    root_dir,
    job_num,
    version="0.0.0",
    build_type="Debug",
    time_trace=False,
    coverage=False,
    jemalloc=False,
    sanitizers=False,
    gtest=False,
    component="all",
    linker="auto",
    cmake_args: dict[str, str] = None,
):
    log.info("Build cpp code in functionsystem")

    # 使用 CMake 创建 Ninja 构建清单
    root_dir = os.path.abspath(root_dir)  # Git根目录
    code_path = os.path.join(root_dir, "functionsystem")
    output_dir = os.path.join(code_path, "output")
    build_dir = os.path.join(code_path, "build")
    cpp_job_num = limit_cpp_job_num(job_num)
    if cmake_args is None:
        cmake_args = {}
    cmake_args.update({
        "BUILD_VERSION": version_name(version),
        "CMAKE_INSTALL_PREFIX": output_dir,
        "CMAKE_BUILD_TYPE": build_type,
        "SANITIZERS": bool2switch(sanitizers),
        "BUILD_LLT": bool2switch(gtest),
        "BUILD_GCOV": bool2switch(coverage),
        "BUILD_THREAD_NUM": cpp_job_num,
        "ROOT_DIR": root_dir,  # 为了数据系统路径
        "JEMALLOC_PROF_ENABLE": bool2switch(jemalloc),
        "FUNCTION_SYSTEM_BUILD_TARGET": component,
        "FUNCTION_SYSTEM_BUILD_LINKER": linker,
        "FUNCTION_SYSTEM_BUILD_TIME_TRACE": bool2switch(time_trace),
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
    })
    cmake_generate(code_path, build_dir, cmake_args)

    # 使用 Ninja 编译程序
    ninja_make(build_dir, str(cpp_job_num), component)

    # 使用 CMake 完成产物复制
    cmake_install(build_dir)


def cmake_generate(source_dir, build_dir, cmake_args: dict[str, str]):
    log.info(f"CMAKE generate Ninja make list with args: {json.dumps(cmake_args)}")
    log.info(f"Run cmake with source code[{source_dir}] to build[{build_dir}]")
    args = []
    for key, val in cmake_args.items():
        k = "-D" + key.upper()
        v = val if val is not None else ""
        args.append(f"{k}={v}")
    utils.sync_command(["cmake", "-G", "Ninja", "-S", source_dir, "-B", build_dir, *args])


def ninja_make(build_dir: str, job_num: str, component: str = "all"):
    log.info(f"Run Ninja build in dir[{build_dir}] using {job_num} cores. Module: {component}")
    command = ["ninja", "-C", build_dir, "-j", job_num]
    if component != "all":
        command.append(component)
    utils.sync_command(command)


def limit_cpp_job_num(job_num):
    try:
        job_num = int(job_num)
    except (TypeError, ValueError):
        log.warning(f"Invalid cpp job num[{job_num}], fallback to 1")
        return 1

    if platform.system().lower() != "linux" or platform.machine().lower() not in {"aarch64", "arm64"}:
        return job_num

    max_jobs = os.getenv("YR_AARCH64_CPP_MAX_JOBS", str(DEFAULT_AARCH64_CPP_MAX_JOBS))
    try:
        max_jobs = int(max_jobs)
    except ValueError:
        log.warning(f"Invalid YR_AARCH64_CPP_MAX_JOBS[{max_jobs}], fallback to {DEFAULT_AARCH64_CPP_MAX_JOBS}")
        max_jobs = DEFAULT_AARCH64_CPP_MAX_JOBS
    if max_jobs <= 0:
        return job_num

    limited_job_num = min(job_num, max_jobs)
    if limited_job_num != job_num:
        log.info(f"Limit aarch64 cpp build jobs from {job_num} to {limited_job_num}")
    return limited_job_num


def cmake_install(build_dir: str):
    log.info(f"Run cmake install in dir[{build_dir}]")
    utils.sync_command(["cmake", "--build", build_dir, "--target", "install"])


def version_name(version):
    return f"yr-functionsystem-v{version}"


def bool2switch(b: bool):
    return "ON" if b else "OFF"
