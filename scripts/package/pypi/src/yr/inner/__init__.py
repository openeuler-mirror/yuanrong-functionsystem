#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

__all__ = ["yuanrong_installation_dir"]

import os

self_dir = os.path.realpath(os.path.dirname(os.path.realpath(__file__)))
yuanrong_installation_dir = os.path.realpath(self_dir)
