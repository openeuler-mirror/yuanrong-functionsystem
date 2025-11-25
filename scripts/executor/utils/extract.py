# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import stat
import utils
import tarfile
import zipfile

log = utils.stream_logger()


def extract_file(archive_path, extract_to):
    if archive_path.endswith(".zip"):
        return extract_zip(archive_path, extract_to)
    elif archive_path.endswith(".tar.gz"):
        return extract_tar(archive_path, extract_to)
    elif archive_path.endswith(".tar"):
        return extract_tar(archive_path, extract_to)
    else:
        raise TypeError(f"Unsupported compression type for file {archive_path.split('/')[-1]}")


def extract_zip(zipfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    zip_ref = zipfile.ZipFile(zipfile_path, 'r')
    zip_ref.extractall(extract_to)
    for info in zip_ref.infolist():
        extracted_path = os.path.join(extract_to, info.filename)
        if not info.is_dir() and info.external_attr:
            unix_permissions = (info.external_attr >> 16) & 0o777
            os.chmod(extracted_path, unix_permissions)
    for info in zip_ref.infolist():
        file_path = os.path.join(extract_to, info.filename)
        if _is_symlink(info):
            # 读取并创建符号链接
            link_target = zip_ref.read(info.filename).decode('utf-8').strip()
            if os.path.lexists(file_path):
                os.remove(file_path)
            os.symlink(link_target, file_path)
    log.info(f"File {zipfile_path.split('/')[-1]} extraction complete")
    return zip_ref.infolist()[0].filename.split('/')[0]


def extract_tar(tarfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    tar_ref = tarfile.open(tarfile_path, 'r:*')
    tar_ref.extractall(extract_to)
    log.info(f"File {tarfile_path.split('/')[-1]} extraction complete")
    return tar_ref.getnames()[0]


def _is_symlink(info):
    """检查 ZIP 条目是否为符号链接"""
    mode = (info.external_attr >> 16) & 0xFFFF
    return stat.S_IFMT(mode) == stat.S_IFLNK
