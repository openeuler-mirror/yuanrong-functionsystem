#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""setup"""

import os
import shutil
import sys
import warnings

import setuptools
from setuptools.command.build_ext import build_ext

ROOT_DIR = os.path.dirname(__file__)


def get_version():
    """get version"""
    version = os.getenv('BUILD_VERSION', "")
    if len(version) == 0:
        return "v0.0.1"
    return version


def copy_file(target, filename, root):
    """copy file"""
    source = os.path.relpath(filename, root)
    dst = os.path.join(target, source)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy(source, dst, follow_symlinks=True)


def run_ext(ctx):
    """run ext"""
    files_to_include = []
    for root, _, fs in os.walk("./yr"):
        for i in fs:
            if "so" in i:
                files_to_include.append(os.path.join(root, i))
    for filename in files_to_include:
        copy_file(ctx.build_lib, filename, ROOT_DIR)


class BuildExtImpl(build_ext):
    """build ext impl"""

    def run(self):
        return run_ext(self)


class BinaryDistribution(setuptools.Distribution):
    """binary distribution"""

    def has_ext_modules(self):
        """has ext modules"""
        return True


if sys.version_info[0] == 3 and sys.version_info[1] == 6:
    requirements_file = "requirements_for_py36.txt"
elif sys.version_info[0] == 3 and sys.version_info[1] == 7:
    requirements_file = "requirements_for_py37.txt"
elif sys.version_info[0] == 3 and sys.version_info[1] == 11:
    requirements_file = "requirements_for_py311.txt"
else:
    requirements_file = "requirements.txt"

with open(os.path.join(ROOT_DIR, requirements_file)) as f:
    requirements = f.read().splitlines()

warnings.filterwarnings("ignore", category=setuptools.SetuptoolsDeprecationWarning)
setuptools.setup(
    name="yr_sdk",
    version=get_version(),
    author="openyuanrong",
    classifiers=[
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
    package_data={
        "yr": ["includes/*.pxd", "*.so.*", "*.so"],
    },
    cmdclass={"build_ext": BuildExtImpl},
    distclass=BinaryDistribution,
    # Make setuptools regard the directory is the top-level directory for yr package building
    packages=setuptools.find_packages(exclude=("tests", "*.tests", "*.tests.*")),
    install_requires=requirements,
    include_package_data=True,
    exclude_package_data={
        "": ["BUILD"],
    },
    extras_require={
        "core": ["yr-core"],
        "serve": ["fastapi"]
    }
)
