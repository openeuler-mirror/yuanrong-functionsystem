import os
import tempfile
import unittest
from unittest import mock

from tasks import build_task


class BuildRuntimeLauncherTest(unittest.TestCase):
    def test_build_runtime_launcher_runs_proto_and_go_commands(self):
        with tempfile.TemporaryDirectory() as root_dir:
            runtime_dir = os.path.join(root_dir, "runtime-launcher")
            os.makedirs(runtime_dir)

            with mock.patch.object(build_task.utils, "sync_command") as sync_command:
                build_task.build_runtime_launcher(root_dir)

            self.assertTrue(os.path.isdir(os.path.join(runtime_dir, "bin", "runtime")))
            self.assertEqual(sync_command.call_count, 5)
            self.assertEqual(
                sync_command.call_args_list[0].args[0],
                ["go", "install", "google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.11"],
            )
            self.assertEqual(
                sync_command.call_args_list[1].args[0],
                ["go", "install", "google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.5.1"],
            )
            self.assertEqual(
                sync_command.call_args_list[2].args[0],
                ["bash", os.path.join("scripts", "generate-proto.sh")],
            )
            self.assertEqual(
                sync_command.call_args_list[3].args[0],
                ["go", "build", "-buildvcs=false", "-o", "bin/runtime/runtime-launcher", "./cmd/runtime-launcher/"],
            )
            self.assertEqual(
                sync_command.call_args_list[4].args[0],
                ["go", "build", "-buildvcs=false", "-o", "bin/rl-client", "./cmd/rl-client/"],
            )
            for call in sync_command.call_args_list:
                self.assertEqual(call.kwargs["cwd"], runtime_dir)
                self.assertIn("/usr/local/go/bin", call.kwargs["env"]["PATH"])
                self.assertEqual(call.kwargs["env"]["GOCACHE"], os.path.join(root_dir, "build", "go-cache"))

    def test_run_build_runtime_launcher_component_only_builds_runtime_launcher(self):
        class Args:
            job_num = 1
            version = "9.9.9"
            build_type = "Release"
            builder = "cmake"
            component = "runtime_launcher"
            linker = "auto"
            cmake_args = []

        with tempfile.TemporaryDirectory() as root_dir:
            os.makedirs(os.path.join(root_dir, "runtime-launcher"))
            with mock.patch.object(build_task, "build_runtime_launcher") as build_runtime_launcher, mock.patch.object(
                build_task, "build_vendor"
            ) as build_vendor:
                build_task.run_build(root_dir, Args())

            build_runtime_launcher.assert_called_once_with(root_dir)
            build_vendor.assert_not_called()

    def test_build_runtime_launcher_filters_empty_path_segments(self):
        with tempfile.TemporaryDirectory() as root_dir:
            runtime_dir = os.path.join(root_dir, "runtime-launcher")
            os.makedirs(runtime_dir)

            with mock.patch.dict(os.environ, {"PATH": ""}, clear=True), mock.patch.object(
                build_task.utils, "sync_command"
            ) as sync_command:
                build_task.build_runtime_launcher(root_dir)

            path_segments = sync_command.call_args_list[0].kwargs["env"]["PATH"].split(":")
            self.assertNotIn("", path_segments)


if __name__ == "__main__":
    unittest.main()
