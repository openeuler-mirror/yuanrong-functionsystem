# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import json

import utils

log = utils.stream_logger()


def compile_cli(root_path):
    app_path = os.path.join(root_path, "functionsystem", "apps", "cli")
    main_path = os.path.join(app_path, "cmd", "main.go")
    output_path = os.path.join(root_path, "functionsystem", "output", "bin")
    compile_golang(app_path, "cli", main_path, output_path)


def compile_golang(app_path, app_name, main_path, output_path,
                   go_ldflags="-s -w", cgo_enabled=False):
    """
    在 app_path 路径下编译 main_path 到 output_path/app_name
    """
    build_env = os.environ.copy()
    build_env["CGO_ENABLED"] = "1" if cgo_enabled else "0"
    bin_path = os.path.join(output_path, app_name)
    log.info(f"Build golang app[{app_name}] to {bin_path}.")
    log.info(f"Go build ldflags: {go_ldflags}")
    log.info(f"Go build env: {json.dumps(build_env)}")
    utils.sync_command(
        cmd=[
            "go", "build",
            "-o", bin_path,
            "-trimpath",
            "-ldflags", go_ldflags,
            main_path
        ],
        cwd=app_path,
        env=build_env
    )
    log.info(f"Build golang app[{app_name}] success")
