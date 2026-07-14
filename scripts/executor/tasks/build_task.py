# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import os
import shutil
import time

import builder
import utils

import tasks
from . import vendor_cache

log = utils.stream_logger()
CXX_MODULES = {
    "all",
    "function_master",
    "domain_scheduler",
    "runtime_manager",
    "function_proxy",
    "function_agent",
    "iam_server",
}
PROTOC_GEN_GO_VERSION = "v1.36.11"
PROTOC_GEN_GO_GRPC_VERSION = "v1.5.1"


def reset_stale_vendor_externalprojects(root_dir, target_names):
    vendor_output = os.path.join(root_dir, "vendor", "output")
    for target_name in sorted(target_names):
        for relpath in (
            os.path.join("Build", target_name),
            os.path.join("Install", target_name),
            os.path.join("Stamp", target_name),
        ):
            abs_path = os.path.join(vendor_output, relpath)
            if os.path.islink(abs_path) or os.path.isfile(abs_path):
                os.unlink(abs_path)
            elif os.path.isdir(abs_path):
                shutil.rmtree(abs_path, ignore_errors=True)


def remove_nonportable_vendor_build_state(vendor_path):
    # CMake-generated files embed absolute workspace paths and cannot be archived.
    shutil.rmtree(os.path.join(vendor_path, "build"), ignore_errors=True)


def run_build(root_dir, cmd_args):
    start_time = time.time()
    builder_name = getattr(cmd_args, "builder", "cmake")
    component = getattr(cmd_args, "component", "all")
    cmake_args = {}
    raw_cmake_args = getattr(cmd_args, "cmake_args", [])
    if isinstance(raw_cmake_args, dict):
        cmake_args.update(raw_cmake_args)
    else:
        for item in raw_cmake_args:
            cmake_args.update(item)
    args = {
        "root_dir": root_dir,
        "job_num": cmd_args.job_num,
        "version": cmd_args.version,
        "build_type": cmd_args.build_type.capitalize(),  # 设置为首字母大写
        "builder": builder_name,
        "component": component,
        "linker": getattr(cmd_args, "linker", "auto"),
        "cmake_args": cmake_args,
    }
    if args["job_num"] > (os.cpu_count() or 1) * 2:
        log.warning(f"The -j {args['job_num']} is over the max logical cpu count({os.cpu_count()}) * 2")
    log.info(f"Start to build function-system with args: {json.dumps(args)}")

    if args["component"] in ["all", "cli"]:
        builder.build_cli(root_dir)
    if args["component"] in ["all", "meta_service"]:
        builder.build_meta_service(root_dir)
    if args["component"] in ["all", "runtime_launcher"]:
        build_runtime_launcher(root_dir)

    if args["component"] in CXX_MODULES:
        build_vendor(args)
        build_litebus(args)
    if args["builder"] == "bazel" and args["component"] in CXX_MODULES:
        # Bazel builds logs, metrics, and C++ binaries from source itself
        build_functionsystem_bazel(root_dir, args)
    elif args["component"] in CXX_MODULES:
        build_logs(args)
        build_metrics(args)
        build_functionsystem(root_dir, args)
    elapsed_time = time.time() - start_time
    log.info(f"Build function-system successfully in {elapsed_time:.2f} seconds")


def build_vendor(args):
    log.info(f"Building vendor with root_dir={args['root_dir']} and job_num={args['job_num']}")
    vendor_path = os.path.join(args["root_dir"], "vendor")
    cache_manager = vendor_cache.VendorCacheManager(args["root_dir"], args["builder"])
    log.info(f"Functionsystem vendor cache root: {cache_manager.cache_root}")

    # 根据下载清单下载第三方依赖
    log.info("Start to download vendor dependency packages")
    tasks.download_vendor(
        config_path=os.path.join(vendor_path, "VendorList.csv"), download_path=os.path.join(vendor_path, "src")
    )
    cache_manager.prepare_workspace()
    reset_stale_vendor_externalprojects(args["root_dir"], cache_manager.misses)

    # 编译三方件依赖
    log.info("Start to build etcd/etcdctl/etcdutl with golang")
    builder.build_etcd(vendor_path)

    log.info("Start to build vendor dependency packages with C++")
    remove_nonportable_vendor_build_state(vendor_path)
    cmake_configure_cmd = ["cmake", "-B", "build", f"-DTHIRDPARTY_JOBS={args['job_num']}"]
    if args.get("builder") == "bazel":
        # Bazel mode: skip vendor components that Bazel builds from source or does not use.
        # See vendor/CMakeLists.txt for the full list of skipped components.
        cmake_configure_cmd.append("-DBAZEL_MODE=ON")
    else:
        # Explicitly override any cached BAZEL_MODE=ON left by a previous bazel-mode build.
        cmake_configure_cmd.append("-DBAZEL_MODE=OFF")
    utils.sync_command(cmake_configure_cmd, cwd=os.path.join(vendor_path))
    utils.sync_command(["cmake", "--build", "build", "--parallel", str(args["job_num"])], cwd=os.path.join(vendor_path))
    cache_manager.publish_workspace()

    # 引入二方件产物
    log.info("Auto install yuanrong-datasystem production from tar file")
    install_datasystem(vendor_path)
    remove_nonportable_vendor_build_state(vendor_path)


def install_datasystem(vendor_path):
    datasystem_sdk_path = os.path.join(vendor_path, "src", "datasystem", "sdk")
    datasystem_install_path = os.path.join(vendor_path, "output", "Install", "datasystem", "sdk")
    if os.path.exists(datasystem_install_path):
        log.warning("Datasystem install path is exist. Skip to copy files.")
        return
    shutil.copytree(datasystem_sdk_path, datasystem_install_path, copy_function=shutil.copy2)


def build_runtime_launcher(root_dir):
    runtime_launcher_dir = os.path.join(root_dir, "runtime-launcher")
    if not os.path.isdir(runtime_launcher_dir):
        raise FileNotFoundError(f"runtime-launcher source directory not found: {runtime_launcher_dir}")

    env = os.environ.copy()
    env.setdefault("HOME", os.path.expanduser("~"))
    env.setdefault("GOCACHE", os.path.join(root_dir, "build", "go-cache"))
    path_entries = [
        "/usr/local/go/bin",
        os.path.expanduser("~/bin"),
        os.path.expanduser("~/go/bin"),
        env.get("PATH", ""),
    ]
    env["PATH"] = ":".join(path for path in path_entries if path)

    os.makedirs(os.path.join(runtime_launcher_dir, "bin", "runtime"), exist_ok=True)
    proto_file = os.path.join("api", "proto", "runtime", "v1", "runtime_launcher.proto")

    log.info("Start to install runtime-launcher protobuf plugins")
    utils.sync_command(
        ["go", "install", f"google.golang.org/protobuf/cmd/protoc-gen-go@{PROTOC_GEN_GO_VERSION}"],
        cwd=runtime_launcher_dir,
        env=env,
    )
    utils.sync_command(
        ["go", "install", f"google.golang.org/grpc/cmd/protoc-gen-go-grpc@{PROTOC_GEN_GO_GRPC_VERSION}"],
        cwd=runtime_launcher_dir,
        env=env,
    )

    log.info("Start to generate runtime-launcher protobuf files")
    utils.sync_command(
        [
            "protoc",
            "--go_out=.",
            "--go_opt=paths=source_relative",
            "--go-grpc_out=.",
            "--go-grpc_opt=paths=source_relative",
            proto_file,
        ],
        cwd=runtime_launcher_dir,
        env=env,
    )

    log.info("Start to build runtime-launcher")
    utils.sync_command(
        ["go", "build", "-buildvcs=false", "-o", "bin/runtime/runtime-launcher", "./cmd/runtime-launcher/"],
        cwd=runtime_launcher_dir,
        env=env,
    )
    utils.sync_command(
        ["go", "build", "-buildvcs=false", "-o", "bin/rl-client", "./cmd/rl-client/"],
        cwd=runtime_launcher_dir,
        env=env,
    )


def build_logs(args):
    log.info("Start to build common/logs")
    utils.sync_command(
        ["bash", "build.sh", "-j", str(args["job_num"])], cwd=os.path.join(args["root_dir"], "common", "logs")
    )


def build_litebus(args):
    log.info("Start to build common/litebus")
    utils.sync_command(
        ["bash", "build.sh", "-t", "off", "-j", str(args["job_num"])],
        cwd=os.path.join(args["root_dir"], "common", "litebus"),
    )


def build_metrics(args):
    log.info("Start to build common/metrics")
    utils.sync_command(
        ["bash", "build.sh", "-j", str(args["job_num"])], cwd=os.path.join(args["root_dir"], "common", "metrics")
    )


def build_functionsystem(root_dir, args):
    log.info("Start to build functionsystem")
    # 编译 CPP 程序
    builder.build_binary(
        root_dir=root_dir,
        job_num=args["job_num"],
        version=args["version"],
        build_type=args["build_type"],
        component=args["component"],
        linker=args["linker"],
        cmake_args=args["cmake_args"],
    )


def build_functionsystem_bazel(root_dir, args):
    log.info("Start to build functionsystem with Bazel")
    # 编译 CPP 程序 (Bazel)
    builder.build_binary_bazel(root_dir, args["job_num"], args["version"], args["build_type"], args["component"])
