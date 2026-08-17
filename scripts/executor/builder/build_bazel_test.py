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
    def test_unstamped_version_header_does_not_contain_git_identity(self):
        with tempfile.TemporaryDirectory() as root_dir:
            template = os.path.join(root_dir, "functionsystem", "src", "common", "utils")
            os.makedirs(template)
            template_path = os.path.join(template, "version.h.in")
            output_path = os.path.join(template, "version.h")
            with open(template_path, "w", encoding="utf-8") as file_obj:
                file_obj.write("@BUILD_VERSION@ @GIT_HASH@ @GIT_BRANCH_NAME@")
            with mock.patch.dict(os.environ, {"FUNCTIONSYSTEM_BUILD_STAMP": "0"}, clear=True):
                build_bazel.generate_version_header(root_dir, "0.0.0")
            with open(output_path, encoding="utf-8") as file_obj:
                content = file_obj.read()

        self.assertIn("unstamped", content)
        self.assertNotIn("refs/heads", content)

    def test_frontend_proxy_proto_is_generated_for_bazel_consumers(self):
        self.assertIn("frontend_proxy_service.proto", build_bazel.PROTO_FILES)
        self.assertIn("frontend_proxy_service.proto", build_bazel.GRPC_PROTO_FILES)

    def test_defaults_keep_workspace_output_and_no_extra_cache_flags(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(build_bazel.resolve_bazel_output_root("/workspace/fs"), "/workspace/fs/build/bazel_root")
            self.assertEqual(build_bazel.bazel_cache_flags(), [])

    def test_buildkite_cache_environment_configures_all_bazel_caches(self):
        env = {
            "FUNCTIONSYSTEM_BAZEL_OUTPUT_ROOT": "/mnt/cache/fs/amd64/output-root",
            "FUNCTIONSYSTEM_BAZEL_REPOSITORY_CACHE": "/mnt/cache/fs/amd64/repository-cache",
            "REMOTE_CACHE": "grpc://bazel-remote:9092",
        }
        with mock.patch.dict(os.environ, env, clear=True):
            self.assertEqual(
                build_bazel.resolve_bazel_output_root("/workspace/fs"), env["FUNCTIONSYSTEM_BAZEL_OUTPUT_ROOT"]
            )
            self.assertEqual(
                build_bazel.bazel_cache_flags(),
                [
                    f"--repository_cache={env['FUNCTIONSYSTEM_BAZEL_REPOSITORY_CACHE']}",
                    f"--remote_cache={env['REMOTE_CACHE']}",
                ],
            )

    def test_local_profiles_share_repository_cache_and_isolate_build_state(self):
        with tempfile.TemporaryDirectory() as cache_root, \
                mock.patch.object(build_bazel.platform, "system", return_value="Linux"), \
                mock.patch.object(build_bazel.platform, "machine", return_value="aarch64"):
            release = build_bazel.resolve_bazel_local_cache("/workspace/fs", cache_root, "release")
            unit_test = build_bazel.resolve_bazel_local_cache("/workspace/fs", cache_root, "ut")

        self.assertEqual(release.repository_cache, unit_test.repository_cache)
        self.assertNotEqual(release.output_root, unit_test.output_root)
        self.assertNotEqual(release.disk_cache, unit_test.disk_cache)
        self.assertTrue(release.output_root.endswith("profiles/release/bazel-output/bazel6-linux-aarch64-v1"))
        self.assertTrue(unit_test.disk_cache.endswith("profiles/ut/bazel-action/bazel6-linux-aarch64-v1"))

    def test_explicit_local_cache_overrides_output_and_repository_environment(self):
        env = {
            "FUNCTIONSYSTEM_BAZEL_OUTPUT_ROOT": "/ci/output-root",
            "FUNCTIONSYSTEM_BAZEL_REPOSITORY_CACHE": "/ci/repository-cache",
            "REMOTE_CACHE": "grpc://bazel-remote:9092",
        }
        with tempfile.TemporaryDirectory() as cache_root, mock.patch.dict(os.environ, env, clear=True):
            local_cache = build_bazel.resolve_bazel_local_cache("/workspace/fs", cache_root, "release")

            self.assertEqual(
                build_bazel.resolve_bazel_output_root("/workspace/fs", local_cache),
                local_cache.output_root,
            )
            self.assertEqual(
                build_bazel.bazel_cache_flags(local_cache),
                [
                    f"--repository_cache={local_cache.repository_cache}",
                    f"--disk_cache={local_cache.disk_cache}",
                    "--deleted_packages=@etcdapi//etcd",
                    f"--remote_cache={env['REMOTE_CACHE']}",
                ],
            )

    def test_local_cache_arguments_must_be_provided_together(self):
        with self.assertRaisesRegex(ValueError, "must be specified together"):
            build_bazel.resolve_bazel_local_cache("/workspace/fs", "/cache", "")
        with self.assertRaisesRegex(ValueError, "must be specified together"):
            build_bazel.resolve_bazel_local_cache("/workspace/fs", "", "release")

    def test_prepare_local_cache_creates_all_derived_directories(self):
        with tempfile.TemporaryDirectory() as cache_root:
            local_cache = build_bazel.resolve_bazel_local_cache("/workspace/fs", cache_root, "ut")
            build_bazel.prepare_bazel_local_cache(local_cache)

            self.assertTrue(os.path.isdir(local_cache.output_root))
            self.assertTrue(os.path.isdir(local_cache.repository_cache))
            self.assertTrue(os.path.isdir(local_cache.disk_cache))

    def test_local_test_profile_preloads_brpc_without_changing_default_test_environment(self):
        with tempfile.TemporaryDirectory() as root_dir:
            brpc_library = os.path.join(root_dir, "functionsystem", "output", "lib", "libbrpc.so")
            os.makedirs(os.path.dirname(brpc_library), exist_ok=True)
            with open(brpc_library, "a", encoding="utf-8"):
                pass
            local_cache = build_bazel.resolve_bazel_local_cache(root_dir, "cache", "ut")

            default_flags = build_bazel.bazel_test_env_flags(root_dir)
            local_flags = build_bazel.bazel_test_env_flags(root_dir, local_cache)

        self.assertFalse(any("LD_PRELOAD" in flag for flag in default_flags))
        self.assertIn(f"--test_env=LD_PRELOAD={brpc_library}", local_flags)

    def test_test_environment_includes_datasystem_sdk_libraries(self):
        with tempfile.TemporaryDirectory() as root_dir:
            datasystem_lib = os.path.join(
                root_dir, "vendor", "output", "Install", "datasystem", "sdk", "cpp", "lib"
            )
            os.makedirs(datasystem_lib)

            flags = build_bazel.bazel_test_env_flags(root_dir)

        self.assertIn(f"--test_env=LD_LIBRARY_PATH={datasystem_lib}", flags)


if __name__ == "__main__":
    unittest.main()
