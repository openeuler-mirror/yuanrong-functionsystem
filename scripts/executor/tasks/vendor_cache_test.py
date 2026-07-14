# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd

import os
import shutil
import tarfile
import tempfile
import unittest

from tasks.vendor_cache import CACHE_ROOT_ENV, READY_MARKER, VendorCacheManager


class VendorCacheManagerTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root_dir = os.path.join(self.temp_dir.name, "root")
        self.cache_root = os.path.join(self.temp_dir.name, "cache")
        self.old_cache_root = os.environ.get(CACHE_ROOT_ENV)
        os.environ[CACHE_ROOT_ENV] = self.cache_root

        vendor_dir = os.path.join(self.root_dir, "vendor")
        os.makedirs(vendor_dir, exist_ok=True)
        for relpath in ("CMakeLists.txt", "VendorList.csv", "vendor_utils.cmake"):
            self._write(os.path.join(vendor_dir, relpath), "test\n")
        os.makedirs(os.path.join(vendor_dir, "cmake"), exist_ok=True)
        os.makedirs(os.path.join(vendor_dir, "patches"), exist_ok=True)

    def tearDown(self):
        if self.old_cache_root is None:
            os.environ.pop(CACHE_ROOT_ENV, None)
        else:
            os.environ[CACHE_ROOT_ENV] = self.old_cache_root
        self.temp_dir.cleanup()

    def test_prepare_rejects_ready_marker_without_securec_artifacts(self):
        manager = VendorCacheManager(self.root_dir, "bazel")
        cache_path = self._cache_entry_path(manager, "securec")
        os.makedirs(cache_path, exist_ok=True)
        self._write(os.path.join(cache_path, READY_MARKER), "ready\n")

        manager.prepare_workspace()

        self.assertIn("securec", manager.misses)
        self.assertNotIn("securec", manager.hits)
        self.assertFalse(os.path.exists(cache_path))
        self.assertFalse(os.path.exists(os.path.join(self.root_dir, "vendor", "output", "Install", "securec")))

    def test_prepare_accepts_complete_securec_cache(self):
        manager = VendorCacheManager(self.root_dir, "bazel")
        cache_path = self._cache_entry_path(manager, "securec")
        self._write(os.path.join(cache_path, "include", "securec.h"), "header\n")
        self._write(os.path.join(cache_path, "lib", "libsecurec.so"), "library\n")
        self._write(os.path.join(cache_path, READY_MARKER), "ready\n")

        manager.prepare_workspace()

        workspace_path = os.path.join(self.root_dir, "vendor", "output", "Install", "securec")
        self.assertIn("securec", manager.hits)
        self.assertTrue(os.path.islink(workspace_path))
        self.assertEqual(os.path.realpath(workspace_path), os.path.realpath(cache_path))

    def test_publish_refuses_incomplete_securec_workspace(self):
        manager = VendorCacheManager(self.root_dir, "bazel")
        workspace_path = os.path.join(self.root_dir, "vendor", "output", "Install", "securec")
        os.makedirs(workspace_path, exist_ok=True)
        manager.misses.add("securec")

        with self.assertRaisesRegex(RuntimeError, "incomplete vendor cache for securec"):
            manager.publish_workspace()

        cache_path = self._cache_entry_path(manager, "securec")
        self.assertFalse(os.path.exists(os.path.join(cache_path, READY_MARKER)))

    def test_default_cache_survives_vendor_archive_workspace_move(self):
        os.environ.pop(CACHE_ROOT_ENV, None)
        source_root = os.path.join(self.temp_dir.name, "source-root")
        self._init_vendor(source_root)

        source_manager = VendorCacheManager(source_root, "bazel")
        expected_cache_root = os.path.join(
            source_root, "vendor", "output", ".functionsystem-vendor-cache"
        )
        self.assertEqual(source_manager.cache_root, expected_cache_root)

        source_manager.prepare_workspace()
        workspace_path = os.path.join(source_root, "vendor", "output", "Install", "securec")
        self._write(os.path.join(workspace_path, "include", "securec.h"), "header\n")
        self._write(os.path.join(workspace_path, "lib", "libsecurec.so"), "library\n")
        source_manager.publish_workspace()

        self.assertTrue(os.path.islink(workspace_path))
        self.assertFalse(os.path.isabs(os.readlink(workspace_path)))

        archive_path = os.path.join(self.temp_dir.name, "vendor.tar.gz")
        with tarfile.open(archive_path, "w:gz", dereference=False) as archive:
            archive.add(os.path.join(source_root, "vendor"), arcname="vendor")

        shutil.rmtree(source_root)
        destination_root = os.path.join(self.temp_dir.name, "destination-root")
        os.makedirs(destination_root)
        with tarfile.open(archive_path, "r:gz") as archive:
            archive.extractall(destination_root)

        destination_manager = VendorCacheManager(destination_root, "bazel")
        destination_manager.prepare_workspace()
        destination_workspace = os.path.join(
            destination_root, "vendor", "output", "Install", "securec"
        )
        self.assertIn("securec", destination_manager.hits)
        self.assertTrue(os.path.isfile(os.path.join(destination_workspace, "include", "securec.h")))

    def _cache_entry_path(self, manager, vendor):
        return manager._cache_entry_path(vendor)  # pylint: disable=protected-access

    def _init_vendor(self, root_dir):
        vendor_dir = os.path.join(root_dir, "vendor")
        os.makedirs(vendor_dir, exist_ok=True)
        for relpath in ("CMakeLists.txt", "VendorList.csv", "vendor_utils.cmake"):
            self._write(os.path.join(vendor_dir, relpath), "test\n")
        os.makedirs(os.path.join(vendor_dir, "cmake"), exist_ok=True)
        os.makedirs(os.path.join(vendor_dir, "patches"), exist_ok=True)

    def _write(self, path, content):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as file_obj:
            file_obj.write(content)


if __name__ == "__main__":
    unittest.main()
