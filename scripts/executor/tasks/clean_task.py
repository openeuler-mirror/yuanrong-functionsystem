# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os.path
import shutil

import utils

log = utils.stream_logger()


def run_clean(root_dir, args):
    args = {
        "root_dir": root_dir,
        "skip_vendor": args.skip_vendor,
        "skip_change": args.skip_change,
    }
    log.info("Start to clean vendor cache and component build cache")
    vendor_path = os.path.join(root_dir, "vendor")
    common_path = os.path.join(root_dir, "common")
    source_path = os.path.join(root_dir, "functionsystem")

    if not args["skip_vendor"]:
        vendor_list = [
            os.path.join(vendor_path, "build"),
            os.path.join(vendor_path, "output"),
            os.path.join(vendor_path, "src", "etcd", "bin"),
        ]
        for folder in vendor_list:
            shutil.rmtree(folder, ignore_errors=True)

    if not args["skip_change"]:
        utils.sync_command(["git", "clean", "-dffx"], cwd=root_dir)

    output_list = [
        os.path.join(source_path, "build"),
        os.path.join(source_path, "output"),
        os.path.join(common_path, "logs", "build"),
        os.path.join(common_path, "logs", "output"),
        os.path.join(common_path, "litebus", "build"),
        os.path.join(common_path, "litebus", "output"),
        os.path.join(common_path, "metrics", "build"),
        os.path.join(common_path, "metrics", "output"),
    ]
    for folder in output_list:
        shutil.rmtree(folder, ignore_errors=True)
