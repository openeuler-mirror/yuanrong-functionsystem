# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import tempfile
import unittest

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


if __name__ == "__main__":
    unittest.main()
