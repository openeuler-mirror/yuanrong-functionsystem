# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import json
import utils
import shutil
import os.path

log = utils.stream_logger()


def run_pack(root_dir, cmd_args):
    args = {
        "root_dir": root_dir,
        "version": cmd_args.version,
        "pack_type": cmd_args.pack_type.capitalize(),  # 设置为首字母大写
    }
    log.info(f"Start to package function system output with args: {json.dumps(args)}")
    renew_output(root_dir)
    pack_metrics(args)
    pack_functionsystem(args)


def renew_output(root_dir):
    # 创建输出文件夹
    output_dir = os.path.join(root_dir, "output")  # ./functionsystem/output
    if os.path.exists(output_dir):
        log.warning(f"Removing product output folder: {output_dir}")
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)


def pack_functionsystem(args):
    root_dir = args['root_dir']
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

    # CPP程序去符号
    if args['pack_type'] == "Release":
        log.info("Remove debug symbols from compiled products")
        remove_cpp_symbol(pack_base_dir)

    # 拷贝系统部署配置
    log.info("Copy function system deploy package")
    deploy_src_path = os.path.join(root_dir, "scripts", "deploy")
    deploy_dst_path = os.path.join(pack_base_dir, "deploy")
    shutil.copytree(deploy_src_path, deploy_dst_path, copy_function=shutil.copy2)

    # 拷贝部署配置文件
    log.info("Copy function system config files")
    config_src_path = os.path.join(root_dir, "scripts", "config")
    config_dst_path = os.path.join(pack_base_dir, "config")
    shutil.copytree(config_src_path, config_dst_path, copy_function=shutil.copy2)

    # 拷贝ETCD二进制
    log.info("Copy etcd binary to vendor folder")
    etcd_bin_path = os.path.join(root_dir, "vendor", "src", "etcd", "bin")
    etcd_dst_path = os.path.join(pack_base_dir, "deploy", "vendor", "etcd")
    shutil.copytree(etcd_bin_path, etcd_dst_path, copy_function=shutil.copy2)

    # 生成压缩包
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
    root_dir = args['root_dir']
    pack_base_dir = os.path.join(root_dir, "output", "metrics")  # ./output/metrics

    # 拷贝配置文件
    log.info("Copy metrics config files")
    config_src_path = os.path.join(root_dir, "scripts", "config", "metrics")
    config_dst_path = os.path.join(pack_base_dir, "config")
    shutil.copytree(config_src_path, config_dst_path, copy_function=shutil.copy2)

    # 拷贝头文件
    log.info("Copy metrics include files")
    include_src_path = os.path.join(root_dir, "common", "metrics", "output", "include")
    include_dst_path = os.path.join(pack_base_dir, "include")
    shutil.copytree(include_src_path, include_dst_path, copy_function=shutil.copy2)

    # 拷贝 lib 库
    log.info("Copy metrics library products")
    lib_src_path = os.path.join(root_dir, "common", "metrics", "output", "lib")
    lib_dst_path = os.path.join(pack_base_dir, "lib")
    shutil.copytree(lib_src_path, lib_dst_path, copy_function=shutil.copy2, symlinks=True)

    # 生成压缩包
    tar_name = f"metrics.tar.gz"
    tarfile = os.path.join(root_dir, "output", tar_name)
    archive_output(tar_name, tarfile, pack_base_dir)


def archive_output(tar_name, tarfile, pack_base_dir):
    log.info(f"Packaging function system compression package to {tarfile}")
    utils.archive_tar(tarfile, pack_base_dir)
    tar_file_size = os.path.getsize(tarfile)
    log.info(f"The size of the product[{tar_name}] is {tar_file_size / (1024 * 1024):.2f}MiB")
