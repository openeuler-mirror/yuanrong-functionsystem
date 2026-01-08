# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import os
import shutil

import builder
import utils

import tasks

log = utils.stream_logger()


def run_build(root_dir, cmd_args):
    args = {
        "root_dir": root_dir,
        "job_num": cmd_args.job_num,
        "version": cmd_args.version,
        "build_type": cmd_args.build_type.capitalize(),  # 设置为首字母大写
    }
    if args["job_num"] > (os.cpu_count() or 1) * 2:
        log.warning(f"The -j {args['job_num']} is over the max logical cpu count({os.cpu_count()}) * 2")
    log.info(f"Start to build function-system with args: {json.dumps(args)}")

    build_vendor(args)
    build_logs(args)
    build_litebus(args)
    build_metrics(args)
    build_functionsystem(root_dir, args)


def build_vendor(args):
    log.info(f"Building vendor with root_dir={args['root_dir']} and job_num={args['job_num']}")
    vendor_path = os.path.join(args["root_dir"], "vendor")

    # 根据下载清单下载第三方依赖
    log.info("Start to download vendor dependency packages")
    tasks.download_vendor(
        config_path=os.path.join(vendor_path, "VendorList.csv"), download_path=os.path.join(vendor_path, "src")
    )

    # 编译三方件依赖
    log.info("Start to build etcd/etcdctl/etcdutl with golang")
    builder.build_etcd(vendor_path)

    log.info("Start to build vendor dependency packages with C++")
    utils.sync_command(["cmake", "-B", "build"], cwd=os.path.join(vendor_path))
    utils.sync_command(["cmake", "--build", "build", "--parallel", str(args["job_num"])], cwd=os.path.join(vendor_path))

    # 引入二方件产物
    log.info("Auto install yuanrong-datasystem production from tar file")
    install_datasystem(vendor_path)


def install_datasystem(vendor_path):
    datasystem_sdk_path = os.path.join(vendor_path, "src", "datasystem", "sdk")
    datasystem_install_path = os.path.join(vendor_path, "output", "Install", "datasystem", "sdk")
    if os.path.exists(datasystem_install_path):
        log.warning("Datasystem install path is exist. Skip to copy files.")
        return
    shutil.copytree(datasystem_sdk_path, datasystem_install_path, copy_function=shutil.copy2)


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
    builder.build_binary(root_dir, args["job_num"], args["version"], args["build_type"])
    # 编译 CLI 程序
    builder.build_cli(root_dir)
    # 编译 meta-service
    builder.build_meta_service(root_dir)
