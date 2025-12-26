# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import utils

log = utils.stream_logger()


def build_wheel(wheel_version, config_path, wheel_path):
    # 检查运行依赖
    for depend, min_version in [("toml", "0.9.0"), ("build", "1.3.0"), ("wheel", "0.36.0"), ("setuptools", "80.9.0")]:
        stat, ver = utils.check_package_metadata(depend)
        if not stat or utils.compare_version(ver, min_version) < 0:
            stat, ver = utils.auto_install_and_upgrade(depend)
        log.info(f"Running dependency {depend} install status is {stat}, version is {ver}")

    import build
    import build.env
    import toml

    # 修改配置文件
    data = toml.load(config_path)
    glibc_version = utils.get_glibc_version().replace(".", "_")
    _, machine_arch, _, _, _ = utils.get_system_info()
    data["project"]["version"] = wheel_version
    data["tool"]["distutils"]["bdist_wheel"]["plat-name"] = f"manylinux_{glibc_version}_{machine_arch}"
    with open(config_path, "w") as f:
        toml.dump(data, f)

    dist_path = build.ProjectBuilder(wheel_path).build("wheel", wheel_path)
    log.info(f"Build function system wheel at {dist_path}")

    return dist_path
