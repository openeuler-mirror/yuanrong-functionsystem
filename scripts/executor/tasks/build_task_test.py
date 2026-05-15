# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
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

    def _write(self, path, content):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as file_obj:
            file_obj.write(content)


if __name__ == "__main__":
    unittest.main()
