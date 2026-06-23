# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
from types import SimpleNamespace
from unittest import mock
import tempfile
import unittest

from tasks import build_task


class BuildTaskTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root_dir = os.path.join(self.temp_dir.name, "root")
        self.vendor_output_dir = os.path.join(self.root_dir, "vendor", "output")

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_reset_stale_externalproject_state_for_cache_miss(self):
        self._write(os.path.join(self.vendor_output_dir, "Stamp", "openssl", "openssl-done"), "done\n")
        self._write(os.path.join(self.vendor_output_dir, "Build", "openssl", "cache.txt"), "build\n")
        self._write(os.path.join(self.vendor_output_dir, "Install", "openssl", "lib", "libssl.so"), "openssl\n")
        self._write(os.path.join(self.vendor_output_dir, "Stamp", "zlib", "zlib-done"), "done\n")

        build_task.reset_stale_vendor_externalprojects(self.root_dir, {"openssl"})

        self.assertFalse(os.path.exists(os.path.join(self.vendor_output_dir, "Stamp", "openssl")))
        self.assertFalse(os.path.exists(os.path.join(self.vendor_output_dir, "Build", "openssl")))
        self.assertFalse(os.path.exists(os.path.join(self.vendor_output_dir, "Install", "openssl")))
        self.assertTrue(os.path.exists(os.path.join(self.vendor_output_dir, "Stamp", "zlib", "zlib-done")))

    def test_cmake_component_build_passes_component_linker_and_cmake_args(self):
        args = SimpleNamespace(
            job_num=8,
            version="1.2.3",
            build_type="debug",
            builder="cmake",
            component="function_proxy",
            linker="mold",
            cmake_args={"fs_fast_debug": "OFF"},
        )

        with mock.patch.object(build_task, "build_vendor") as build_vendor, \
                mock.patch.object(build_task, "build_litebus") as build_litebus, \
                mock.patch.object(build_task, "build_logs") as build_logs, \
                mock.patch.object(build_task, "build_metrics") as build_metrics, \
                mock.patch.object(build_task.builder, "build_binary") as build_binary, \
                mock.patch.object(build_task.builder, "build_cli") as build_cli, \
                mock.patch.object(build_task.builder, "build_meta_service") as build_meta_service:
            build_task.run_build(self.root_dir, args)

        build_vendor.assert_called_once()
        build_litebus.assert_called_once()
        build_logs.assert_called_once()
        build_metrics.assert_called_once()
        build_cli.assert_not_called()
        build_meta_service.assert_not_called()
        build_binary.assert_called_once_with(
            root_dir=self.root_dir,
            job_num=8,
            version="1.2.3",
            build_type="Debug",
            component="function_proxy",
            linker="mold",
            cmake_args={"fs_fast_debug": "OFF"},
        )

    def _write(self, path, content):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as file_obj:
            file_obj.write(content)


if __name__ == "__main__":
    unittest.main()
