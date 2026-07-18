# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import tempfile
import unittest
from unittest import mock

from builder import build_bazel


class SharedGrpcRuntimeTest(unittest.TestCase):
    def test_configure_shared_grpc_runtime_replaces_public_targets_idempotently(self):
        source = """grpc_cc_library(
    name = \"grpc\",
    deps = [\"core(target)\"],
)

grpc_cc_library(
    name = \"gpr\",
)

grpc_cc_library(
    name = \"grpc++\",
    deps = [\"grpc++_base\"],
)
"""
        with tempfile.TemporaryDirectory() as root_dir:
            build_path = os.path.join(root_dir, "vendor", "src", "grpc", "BUILD")
            os.makedirs(os.path.dirname(build_path), exist_ok=True)
            with open(build_path, "w", encoding="utf-8") as file_obj:
                file_obj.write(source)

            build_bazel.configure_shared_grpc_runtime(root_dir)
            build_bazel.configure_shared_grpc_runtime(root_dir)

            with open(build_path, "r", encoding="utf-8") as file_obj:
                updated = file_obj.read()

        self.assertEqual(updated.count('actual = "@grpc_runtime//:grpc"'), 1)
        self.assertEqual(updated.count('actual = "@grpc_runtime//:grpcpp"'), 1)
        self.assertEqual(updated.count('actual = "@grpc_runtime//:gpr"'), 1)
        self.assertNotIn('deps = ["core(target)"]', updated)


class BazelCacheConfigTest(unittest.TestCase):
    def test_defaults_keep_workspace_output_and_no_extra_cache_flags(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(build_bazel._bazel_output_root("/workspace/fs"), "/workspace/fs/build/bazel_root")
            self.assertEqual(build_bazel._bazel_cache_flags(), [])

    def test_buildkite_cache_environment_configures_all_bazel_caches(self):
        env = {
            "FUNCTIONSYSTEM_BAZEL_OUTPUT_ROOT": "/mnt/cache/fs/amd64/output-root",
            "FUNCTIONSYSTEM_BAZEL_REPOSITORY_CACHE": "/mnt/cache/fs/amd64/repository-cache",
            "REMOTE_CACHE": "grpc://bazel-remote:9092",
        }
        with mock.patch.dict(os.environ, env, clear=True):
            self.assertEqual(build_bazel._bazel_output_root("/workspace/fs"), env["FUNCTIONSYSTEM_BAZEL_OUTPUT_ROOT"])
            self.assertEqual(
                build_bazel._bazel_cache_flags(),
                [
                    f"--repository_cache={env['FUNCTIONSYSTEM_BAZEL_REPOSITORY_CACHE']}",
                    f"--remote_cache={env['REMOTE_CACHE']}",
                ],
            )


if __name__ == "__main__":
    unittest.main()
