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

import os
import warnings

from setuptools import setup, find_packages, Distribution, SetuptoolsDeprecationWarning

version = os.environ.get("YR_BUILD_VERSION", "1.0.0.dev")
requirements = os.environ.get("YR_REQUIREMENTS", "").splitlines()
package_name = os.environ.get("YR_PACKAGE_NAME", "yr")


class BinaryDistribution(Distribution):
    """binary distribution"""
    def has_ext_modules(self):
        """has ext modules"""
        return True

if __name__ == '__main__':
    warnings.filterwarnings("ignore", category=SetuptoolsDeprecationWarning)
    setup(
        name=package_name,
        version=version,
        classifiers=[
            "Programming Language :: Python :: 3.9",
            "Programming Language :: Python :: 3.8",
        ],
        packages=find_packages(),
        include_package_data=True,
        package_data={
            "yr": ["*"],
        },
        distclass=BinaryDistribution,
        install_requires=requirements,
        setup_requires=[],
        entry_points={
            "console_scripts": [
                "yr=yr.yr_wrapper:run_yr",
            ]
        },
        maintainer="yuanrong team",
    )
