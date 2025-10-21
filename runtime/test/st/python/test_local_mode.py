#!/usr/bin/env python3
# coding=UTF-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import pytest

import yr


@pytest.mark.skip(
    reason="No usage scenario."
)
def test_local_invoke(init_yr_with_local_mode):
    @yr.invoke
    def func():
        return 1

    assert yr.get(func.invoke()) == 1
