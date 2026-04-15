# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import os.path
import shutil

import builder
import utils

log = utils.stream_logger()


def run_pack(root_dir, cmd_args):
    args = {
        "root_dir": root_dir,
        "version": cmd_args.version,
        "skip_wheel": cmd_args.skip_wheel,
        "skip_metrics": cmd_args.skip_metrics,
        "skip_archive": cmd_args.skip_archive,
        "strip_symbols": cmd_args.strip_symbols,
    }
    log.info(f"Start to package function system output with args: {json.dumps(args)}")
    renew_output(root_dir)

    log.info("Start packaging function system compilation products")
    pack_functionsystem(args)

    if args["skip_wheel"] is False:
        log.info("Start building and packaging Python wheel product")
        pack_wheel(args)
    if args["skip_metrics"] is False:
        log.info("Start packaging common metrics product")
        pack_metrics(args)


def renew_output(root_dir):
    # 创建输出文件夹
    log.warning("Deleting output folder and rebuilding it")
    output_dir = os.path.join(root_dir, "output")  # ./functionsystem/output
    if os.path.exists(output_dir):
        log.warning(f"Removing product output folder: {output_dir}")
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)


def pack_functionsystem(args):
    root_dir = args["root_dir"]
    pack_base_dir = os.path.join(root_dir, "output", "functionsystem")  # ./output/functionsystem

    # 拷贝二进制 bin 产物
    # CPP编译产物目录 ./functionsystem/output/bin
    # GO编译产物目录 暂无
    # 打包产物目录 ./output/functionsystem/bin
    log.info("Copy function system binary products")
    bin_src_path = os.path.join(root_dir, "functionsystem", "output", "bin")
    bin_dst_path = os.path.join(pack_base_dir, "bin")
    shutil.copytree(bin_src_path, bin_dst_path, copy_function=shutil.copy2)

    # 拷贝动态库 lib 产物
    log.info("Copy function system library products")
    lib_src_path = os.path.join(root_dir, "functionsystem", "output", "lib")
    lib_dst_path = os.path.join(pack_base_dir, "lib")
    shutil.copytree(lib_src_path, lib_dst_path, copy_function=shutil.copy2, symlinks=True)

    # 拷贝运维脚本 产物
    log.info("Copy function system ops tools products")
    lib_src_path = os.path.join(root_dir, "tools", "ops")
    lib_dst_path = os.path.join(pack_base_dir, "tools")
    shutil.copytree(lib_src_path, lib_dst_path, copy_function=shutil.copy2, symlinks=True)

    # CPP程序去符号
    if args["strip_symbols"] is True:
        log.info("Remove debug symbols from compiled products")
        remove_cpp_symbol(pack_base_dir)

    # 拷贝系统部署配置
    log.info("Copy function system deploy package")
    deploy_src_path = os.path.join(root_dir, "scripts", "deploy")
    deploy_dst_path = os.path.join(pack_base_dir, "deploy")
    shutil.copytree(deploy_src_path, deploy_dst_path, copy_function=shutil.copy2)

    # 拷贝部署配置文件
    log.info("Copy function system config files")
    for config_name in ["meta_service", "metrics"]:
        config_src_path = os.path.join(root_dir, "scripts", "config", config_name)
        config_dst_path = os.path.join(pack_base_dir, "config", config_name)
        shutil.copytree(config_src_path, config_dst_path, copy_function=shutil.copy2)

    # 拷贝ETCD二进制
    log.info("Copy etcd binary to vendor folder")
    etcd_bin_path = os.path.join(root_dir, "vendor", "src", "etcd", "bin")
    etcd_dst_path = os.path.join(pack_base_dir, "deploy", "vendor", "etcd")
    shutil.copytree(etcd_bin_path, etcd_dst_path, copy_function=shutil.copy2)

    # 生成压缩包
    if args["skip_archive"] is False:
        tar_name = f"yr-functionsystem-v{args['version']}.tar.gz"
        tarfile = os.path.join(root_dir, "output", tar_name)
        archive_output(tar_name, tarfile, pack_base_dir)


def remove_cpp_symbol(pack_base_dir):
    bin_dir = os.path.join(pack_base_dir, "bin")
    sym_dir = os.path.join(pack_base_dir, "sym")
    if not os.path.exists(sym_dir):
        os.makedirs(sym_dir)

    for file in os.listdir(bin_dir):
        file_path = os.path.join(bin_dir, file)
        basename = os.path.splitext(file)[0]
        sym_file = os.path.join(sym_dir, f"{basename}.sym")
        log.info(f"Remove cpp file[{basename}] symbol to [{sym_file}]")
        utils.sync_command(["objcopy", "--only-keep-debug", file_path, sym_file])
        utils.sync_command(["objcopy", "--add-gnu-debuglink", sym_file, file_path])
        utils.sync_command(["objcopy", "--strip-all", file_path])


def pack_metrics(args):
    root_dir = args["root_dir"]
    pack_base_dir = os.path.join(root_dir, "output", "metrics")  # ./output/metrics

    # Check if C++ metrics build output exists (not produced by Rust builder)
    include_src_path = os.path.join(root_dir, "common", "metrics", "output", "include")
    lib_src_path = os.path.join(root_dir, "common", "metrics", "output", "lib")
    if not os.path.exists(include_src_path) or not os.path.exists(lib_src_path):
        log.warning("Metrics build output not found (expected for Rust builder), skipping metrics packaging")
        return

    # 拷贝配置文件
    log.info("Copy metrics config files")
    config_src_path = os.path.join(root_dir, "scripts", "config", "metrics")
    config_dst_path = os.path.join(pack_base_dir, "config")
    shutil.copytree(config_src_path, config_dst_path, copy_function=shutil.copy2)

    # 拷贝头文件
    log.info("Copy metrics include files")
    include_dst_path = os.path.join(pack_base_dir, "include")
    shutil.copytree(include_src_path, include_dst_path, copy_function=shutil.copy2)

    # 拷贝 lib 库
    log.info("Copy metrics library products")
    lib_dst_path = os.path.join(pack_base_dir, "lib")
    shutil.copytree(lib_src_path, lib_dst_path, copy_function=shutil.copy2, symlinks=True)

    # 生成压缩包
    if args["skip_archive"] is False:
        tar_name = "metrics.tar.gz"
        tarfile = os.path.join(root_dir, "output", tar_name)
        archive_output(tar_name, tarfile, pack_base_dir)


def archive_output(tar_name, tarfile, pack_base_dir):
    log.info(f"Packaging function system compression package to {tarfile}")
    utils.archive_tar(tarfile, pack_base_dir)
    tar_file_size = os.path.getsize(tarfile)
    log.info(f"The size of the product[{tar_name}] is {tar_file_size / (1024 * 1024):.2f}MiB")


def pack_wheel(args):
    root_dir = args["root_dir"]
    tar_base_dir = os.path.join(root_dir, "output", "functionsystem")  # ./output/functionsystem
    wheel_base_dir = os.path.join(root_dir, "output", "wheel")  # ./output/wheel
    wheel_fs_dir = os.path.join(wheel_base_dir, "yr", "functionsystem")
    os.makedirs(wheel_fs_dir)

    # 创建软连接（软连接在打包时会自动转为实际链接）
    for name in ["bin", "lib", "config"]:
        src_path = os.path.join(tar_base_dir, name)
        dst_path = os.path.join(wheel_fs_dir, name)
        os.symlink(src_path, dst_path)

    # 复制配置文件
    config_src_path = os.path.join(root_dir, "scripts", "config", "pyproject.toml")
    config_dst_path = os.path.join(wheel_base_dir, "pyproject.toml")
    shutil.copy2(config_src_path, config_dst_path)

    # 复制README
    readme_src_path = os.path.join(root_dir, "README.md")
    readme_dst_path = os.path.join(wheel_base_dir, "README.md")
    shutil.copy2(readme_src_path, readme_dst_path)

    # 构建wheel包
    wheel_src_path = builder.build_wheel(args["version"], config_dst_path, wheel_base_dir)
    wheel_dst_path = os.path.join(root_dir, "output", os.path.basename(wheel_src_path))
    shutil.move(wheel_src_path, wheel_dst_path)
