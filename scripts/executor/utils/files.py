# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
import os
import shutil


def copy2_skip_exists(src, dst, *, follow_symlinks=True):
    """
    目标文件不存在时复制
    """
    if os.path.exists(dst):
        return dst

    return shutil.copy2(src, dst, follow_symlinks=follow_symlinks)


def copy2_when_modify(src, dst, *, follow_symlinks=True):
    """
    来源发生修改或目标不存在时复制
    """
    if os.path.exists(dst) and os.path.getmtime(src) == os.path.getmtime(dst):
        return dst

    return shutil.copy2(src, dst, follow_symlinks=follow_symlinks)
