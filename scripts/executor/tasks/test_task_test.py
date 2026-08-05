# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

from tasks import test_task


class TestTaskTest(unittest.TestCase):
    def test_bazel_test_forwards_opt_in_local_cache_to_build_and_run(self):
        with tempfile.TemporaryDirectory() as root_dir:
            args = SimpleNamespace(
                action="all",
                builder="bazel",
                job_num=8,
                test_suite="ExampleSuite",
                test_case="ExampleCase",
                exec_timeout=120,
                retry_times=0,
                print_logs=True,
                bazel_local_cache_root="/cache/functionsystem",
                bazel_cache_profile="ut",
            )

            with mock.patch.object(test_task.builder, "build_gtest_bazel") as bazel_build, \
                    mock.patch.object(test_task.builder, "run_gtest_bazel") as bazel_run:
                test_task.run_test(root_dir, args)

        bazel_build.assert_called_once_with(
            root_dir,
            8,
            bazel_local_cache_root="/cache/functionsystem",
            bazel_cache_profile="ut",
        )
        bazel_run.assert_called_once_with(
            root_dir,
            8,
            "ExampleSuite",
            "ExampleCase",
            bazel_local_cache_root="/cache/functionsystem",
            bazel_cache_profile="ut",
        )

    def test_cmake_test_ignores_absent_bazel_cache_arguments(self):
        with tempfile.TemporaryDirectory() as root_dir:
            args = SimpleNamespace(
                action="make",
                builder="cmake",
                job_num=4,
                test_suite="*",
                test_case="*",
                exec_timeout=120,
                retry_times=0,
                print_logs=True,
            )

            with mock.patch.object(test_task.builder, "build_gtest") as cmake_build, \
                    mock.patch.object(test_task.builder, "build_gtest_bazel") as bazel_build:
                test_task.run_test(root_dir, args)

        cmake_build.assert_called_once_with(root_dir, 4)
        bazel_build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
