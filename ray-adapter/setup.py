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
import setuptools


def get_version():
    """get version"""
    version = os.getenv('BUILD_VERSION', "")
    if len(version) == 0:
        return "1.0.0"
    return version


with open("requirements.txt") as f:
    requirements = f.read().splitlines()

setuptools.setup(
    name="ray_adapter",
    version=get_version(),
    author="OpenYuanRong",
    classifiers=[
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
    # Make setuptools regard the directory is the top-level directory for yr package building
    packages=setuptools.find_packages(exclude=("tests", "*.tests", "*.tests.*")),
    install_requires=requirements,
    description="Library to adapt the ray",
    long_description=open("README.md").read(),
    license="Apache 2.0",
)
