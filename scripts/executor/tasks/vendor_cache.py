# coding=UTF-8
# Copyright (c) 2025 Huawei Technologies Co., Ltd
from __future__ import annotations

import fcntl
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import tempfile
from dataclasses import dataclass

import utils

log = utils.stream_logger()

CACHE_ROOT_ENV = "FS_VENDOR_CACHE_DIR"
DEFAULT_CACHE_ROOT = "/tmp/functionsystem-vendor-cache"
READY_MARKER = ".cache_ready"


@dataclass(frozen=True)
class VendorTarget:
    name: str
    workspace_relpath: str
    command_fingerprints: tuple[str, ...] = ("cc", "cxx", "cmake")
    required_paths: tuple[str, ...] = ()


def _install_target(name: str, required_paths: tuple[str, ...] = ()) -> VendorTarget:
    return VendorTarget(name=name, workspace_relpath=f"output/Install/{name}", required_paths=required_paths)


ALL_VENDOR_TARGETS = {
    "openssl": _install_target("openssl", ("include/openssl/ssl.h", "lib/libssl.so", "lib/libcrypto.so")),
    "securec": _install_target("securec", ("include/securec.h", "lib/libsecurec.so")),
    "spdlog": _install_target("spdlog", ("include/spdlog/spdlog.h", "lib/libspdlog.so")),
    "zlib": _install_target("zlib", ("include/zlib.h", "lib/libz.so", "lib/libminizip.a")),
    "cjson": _install_target("cjson", ("include/nlohmann/json.hpp",)),
    "yaml": _install_target("yaml", ("include/yaml-cpp/yaml.h", "lib/libyaml-cpp.so")),
    "jemalloc": _install_target("jemalloc", ("include/jemalloc/jemalloc.h", "lib/libjemalloc.so")),
    "gtest_1_10_0": _install_target("gtest_1_10_0", ("include/gtest/gtest.h", "lib/libgtest.a")),
    "gtest_1_12_1": _install_target("gtest_1_12_1", ("include/gtest/gtest.h", "lib/libgtest.a")),
    "absl": _install_target("absl", ("include/absl/base/config.h", "lib/libabsl_base.a")),
    "protobuf": _install_target("protobuf", ("include/google/protobuf/message.h", "lib/libprotobuf.a")),
    "c-ares": _install_target("c-ares", ("include/ares.h", "lib/libcares.a")),
    "re2": _install_target("re2", ("include/re2/re2.h", "lib/libre2.a")),
    "grpc": _install_target("grpc", ("include/grpc/grpc.h", "lib/libgrpc.so")),
    "curl": _install_target("curl", ("include/curl/curl.h", "lib/libcurl.so")),
    "obs": _install_target("obs", ("include/eSDKOBS.h", "lib/libeSDKOBS.so")),
    "etcdapi": _install_target("etcdapi", ("include/etcd/api/etcdserverpb/rpc.grpc.pb.h", "lib/libetcdapi_proto.a")),
    "opentelemetry": _install_target(
        "opentelemetry", ("include/opentelemetry/version.h", "lib/libopentelemetry_trace.so")
    ),
}
ALL_VENDOR_TARGETS["etcd-bin"] = VendorTarget(
    name="etcd-bin",
    workspace_relpath="src/etcd/bin",
    command_fingerprints=("go",),
    required_paths=("etcd", "etcdctl", "etcdutl"),
)

BAZEL_REQUIRED_TARGETS = ("openssl", "securec", "spdlog", "zlib", "curl", "obs", "etcd-bin")
SHARED_VENDOR_INPUTS = ("CMakeLists.txt", "VendorList.csv", "vendor_utils.cmake", "cmake", "patches")
FINGERPRINT_ENVS = ("CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS", "GOFLAGS", "CGO_ENABLED")


def resolve_cache_root() -> str:
    return os.path.abspath(os.environ.get(CACHE_ROOT_ENV, DEFAULT_CACHE_ROOT))


class VendorCacheManager:
    def __init__(self, root_dir: str, builder: str):
        self.root_dir = os.path.abspath(root_dir)
        self.vendor_dir = os.path.join(self.root_dir, "vendor")
        self.builder = builder
        self.cache_root = resolve_cache_root()
        self.install_root = os.path.join(self.vendor_dir, "output", "Install")
        self.openEuler_root = os.path.join(self.vendor_dir, "output", "openEuler")
        self.targets = {name: ALL_VENDOR_TARGETS[name] for name in self._required_target_names()}
        self.keys: dict[str, str] = {}
        self.hits: set[str] = set()
        self.misses: set[str] = set()
        self.shared_input_fingerprint = self._shared_input_fingerprint()
        os.makedirs(self.cache_root, exist_ok=True)
        os.makedirs(self._lock_dir(), exist_ok=True)

    def prepare_workspace(self):
        self._ensure_output_layout()
        for name, target in self.targets.items():
            cache_path = self._cache_entry_path(name)
            workspace_path = self._workspace_path(target)
            if self._is_cache_ready(name, cache_path):
                self._materialize_symlink(workspace_path, cache_path)
                self.hits.add(name)
                log.info(f"Vendor cache hit: {name} -> {cache_path}")
            else:
                if os.path.exists(cache_path):
                    with self._flock(name):
                        if not self._is_cache_ready(name, cache_path):
                            log.warning(f"Remove invalid vendor cache for {name}: {cache_path}")
                            self._remove_path(cache_path)
                self._remove_path(workspace_path)
                self.misses.add(name)
                log.info(f"Vendor cache miss: {name}")
        self._ensure_output_layout()

    def publish_workspace(self):
        for name in sorted(self.misses):
            target = self.targets[name]
            workspace_path = self._workspace_path(target)
            if not os.path.exists(workspace_path):
                log.warning(f"Skip publishing vendor cache for {name}: workspace output not found at {workspace_path}")
                continue
            if not self._is_target_output_valid(name, workspace_path):
                raise RuntimeError(f"Refuse to publish incomplete vendor cache for {name}: {workspace_path}")
            cache_path = self._cache_entry_path(name)
            with self._flock(name):
                if not self._is_cache_ready(name, cache_path):
                    self._publish_directory(name, workspace_path, cache_path)
                    log.info(f"Published vendor cache: {name} -> {cache_path}")
                self._materialize_symlink(workspace_path, cache_path)
        self._ensure_output_layout()

    def _required_target_names(self) -> tuple[str, ...]:
        if self.builder == "bazel":
            return BAZEL_REQUIRED_TARGETS
        return tuple(ALL_VENDOR_TARGETS.keys())

    def _cache_entry_path(self, name: str) -> str:
        return os.path.join(self.cache_root, f"{name}_{self._cache_key(name)}")

    def _cache_key(self, name: str) -> str:
        if name in self.keys:
            return self.keys[name]
        target = self.targets[name]
        payload = {
            "name": target.name,
            "workspace_relpath": target.workspace_relpath,
            "vendor_inputs": self.shared_input_fingerprint,
            "commands": {command: self._command_fingerprint(command) for command in target.command_fingerprints},
            "env": {env: os.environ.get(env, "") for env in FINGERPRINT_ENVS},
        }
        key = hashlib.sha256(json.dumps(payload, sort_keys=True).encode("utf-8")).hexdigest()
        self.keys[name] = key
        return key

    def _shared_input_fingerprint(self) -> dict[str, str]:
        return {relpath: self._path_fingerprint(os.path.join(self.vendor_dir, relpath)) for relpath in SHARED_VENDOR_INPUTS}

    def _workspace_path(self, target: VendorTarget) -> str:
        return os.path.join(self.vendor_dir, target.workspace_relpath)

    def _ensure_output_layout(self):
        os.makedirs(self.install_root, exist_ok=True)
        os.makedirs(self.openEuler_root, exist_ok=True)
        self._materialize_symlink(os.path.join(self.openEuler_root, "Install"), self.install_root)

    def _is_cache_ready(self, name: str, cache_path: str) -> bool:
        return os.path.exists(os.path.join(cache_path, READY_MARKER)) and self._is_target_output_valid(name, cache_path)

    def _is_target_output_valid(self, name: str, root_path: str) -> bool:
        if not os.path.isdir(root_path):
            return False
        target = self.targets[name]
        if target.required_paths:
            return all(os.path.isfile(os.path.join(root_path, relpath)) for relpath in target.required_paths)
        return self._has_payload(root_path)

    def _has_payload(self, root_path: str) -> bool:
        for current_root, dirnames, filenames in os.walk(root_path):
            dirnames[:] = [dirname for dirname in dirnames if dirname != ".staging"]
            for filename in filenames:
                if filename != READY_MARKER:
                    return True
            for dirname in dirnames:
                if os.listdir(os.path.join(current_root, dirname)):
                    return True
        return False

    def _publish_directory(self, name: str, src_dir: str, dst_dir: str):
        if not self._is_target_output_valid(name, src_dir):
            raise RuntimeError(f"Refuse to publish incomplete vendor cache for {name}: {src_dir}")
        if os.path.exists(dst_dir) and not self._is_cache_ready(name, dst_dir):
            self._remove_path(dst_dir)
        if os.path.exists(dst_dir):
            return
        staging_root = os.path.join(self.cache_root, ".staging")
        os.makedirs(staging_root, exist_ok=True)
        temp_dir = tempfile.mkdtemp(prefix="vendor-cache-", dir=staging_root)
        try:
            snapshot_dir = os.path.join(temp_dir, "snapshot")
            shutil.copytree(src_dir, snapshot_dir, symlinks=True)
            if not self._is_target_output_valid(name, snapshot_dir):
                raise RuntimeError(f"Refuse to publish incomplete vendor cache snapshot for {name}: {src_dir}")
            with open(os.path.join(snapshot_dir, READY_MARKER), "w", encoding="utf-8") as marker:
                marker.write("ready\n")
            os.replace(snapshot_dir, dst_dir)
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)

    def _materialize_symlink(self, link_path: str, target_path: str):
        if os.path.islink(link_path):
            if os.path.realpath(link_path) == os.path.realpath(target_path):
                return
            os.unlink(link_path)
        elif os.path.exists(link_path):
            self._remove_path(link_path)
        os.makedirs(os.path.dirname(link_path), exist_ok=True)
        os.symlink(target_path, link_path)

    def _remove_path(self, path: str):
        if os.path.islink(path) or os.path.isfile(path):
            os.unlink(path)
        elif os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)

    def _path_fingerprint(self, path: str) -> str:
        if os.path.isdir(path):
            return self._tree_hash(path)
        return self._file_hash(path)

    def _tree_hash(self, root_path: str) -> str:
        sha256 = hashlib.sha256()
        for current_root, dirnames, filenames in os.walk(root_path):
            dirnames.sort()
            filenames.sort()
            rel_root = os.path.relpath(current_root, root_path)
            sha256.update(rel_root.encode("utf-8"))
            for filename in filenames:
                file_path = os.path.join(current_root, filename)
                rel_path = os.path.relpath(file_path, root_path)
                sha256.update(rel_path.encode("utf-8"))
                sha256.update(self._file_hash(file_path).encode("utf-8"))
        return sha256.hexdigest()

    def _file_hash(self, path: str) -> str:
        sha256 = hashlib.sha256()
        with open(path, "rb") as file_obj:
            for block in iter(lambda: file_obj.read(8192), b""):
                sha256.update(block)
        return sha256.hexdigest()

    def _command_fingerprint(self, command_name: str) -> dict[str, str]:
        command = command_name
        if command_name == "cc":
            command = os.environ.get("CC", "cc")
        elif command_name == "cxx":
            command = os.environ.get("CXX", "c++")
        argv = shlex.split(command)
        executable = shutil.which(argv[0]) or argv[0]
        try:
            proc = subprocess.run(
                [*argv, "--version"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=True,
            )
            output = proc.stdout.strip()
        except Exception:
            output = ""
        return {
            "command": command,
            "executable": executable,
            "version": output,
        }

    def _lock_dir(self) -> str:
        return os.path.join(self.cache_root, ".locks")

    def _flock(self, name: str):
        lock_path = os.path.join(self._lock_dir(), f"{name}_{self._cache_key(name)}.lock")
        return _FileLock(lock_path)


class _FileLock:
    def __init__(self, lock_path: str):
        self.lock_path = lock_path
        self.handle = None

    def __enter__(self):
        os.makedirs(os.path.dirname(self.lock_path), exist_ok=True)
        self.handle = open(self.lock_path, "w", encoding="utf-8")
        fcntl.flock(self.handle.fileno(), fcntl.LOCK_EX)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.handle is not None:
            fcntl.flock(self.handle.fileno(), fcntl.LOCK_UN)
            self.handle.close()
            self.handle = None
