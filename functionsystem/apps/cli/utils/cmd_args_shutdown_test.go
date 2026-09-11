/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package utils

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"testing"
	"time"
)

// The helper is a separate supervisor process so SIGTERM cannot kill the test
// runner. The shell models a deployment that must finish deregistration first.
func TestBlockedCommandSignalHelper(t *testing.T) {
	dir := os.Getenv("YR_TEST_SHUTDOWN_DIR")
	if dir == "" {
		return
	}
	cmd := exec.Command("/bin/bash", "-c", `
trap 'touch "$1/terminating"; while [ ! -f "$1/release" ]; do sleep 0.02; done; touch "$1/unregistered"; exit 0' TERM INT
touch "$1/ready"
while :; do sleep 0.02; done
`, "supervisor", dir)
	err, done := ExecCommandUntil(cmd, func(ctx context.Context, _ bool) error {
		if os.Getenv("YR_TEST_SHUTDOWN_AFTER_READY") == "1" {
			for {
				if _, err := os.Stat(filepath.Join(dir, "ready")); err == nil {
					return nil
				}
				select {
				case <-ctx.Done():
					return ctx.Err()
				case <-time.After(10 * time.Millisecond):
				}
			}
		}
		<-ctx.Done()
		return ctx.Err()
	}, 0, true)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "supervising"), nil, 0600); err != nil {
		t.Fatal(err)
	}
	// This must not block when the child exited before startup completed.
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

func TestBlockedCommandWaitsForGracefulShutdown(t *testing.T) {
	for _, sig := range []syscall.Signal{syscall.SIGTERM, syscall.SIGINT} {
		t.Run(sig.String(), func(t *testing.T) {
			dir := t.TempDir()
			cmd := exec.Command(os.Args[0], "-test.run=^TestBlockedCommandSignalHelper$")
			cmd.Env = append(os.Environ(), "YR_TEST_SHUTDOWN_DIR="+dir)
			cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
			cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
			if err := cmd.Start(); err != nil {
				t.Fatal(err)
			}
			defer syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
			done := make(chan error, 1)
			go func() { done <- cmd.Wait() }()
			waitForShutdownFile(t, filepath.Join(dir, "ready"), done)
			if os.Getenv("YR_TEST_SHUTDOWN_AFTER_READY") == "1" {
				waitForShutdownFile(t, filepath.Join(dir, "supervising"), done)
			}
			if err := cmd.Process.Signal(sig); err != nil {
				t.Fatal(err)
			}
			waitForShutdownFile(t, filepath.Join(dir, "terminating"), done)
			select {
			case err := <-done:
				t.Fatalf("supervisor exited before deregistration: %v", err)
			case <-time.After(100 * time.Millisecond):
			}
			if err := os.WriteFile(filepath.Join(dir, "release"), nil, 0600); err != nil {
				t.Fatal(err)
			}
			select {
			case err := <-done:
				if err != nil {
					t.Fatal(err)
				}
			case <-time.After(5 * time.Second):
				t.Fatal("supervisor did not exit after deregistration")
			}
			if _, err := os.Stat(filepath.Join(dir, "unregistered")); err != nil {
				t.Fatal("deregistration was not completed:", err)
			}
		})
	}
}

func TestBlockedCommandWaitsForShutdownAfterReady(t *testing.T) {
	t.Setenv("YR_TEST_SHUTDOWN_AFTER_READY", "1")
	TestBlockedCommandWaitsForGracefulShutdown(t)
}

func waitForShutdownFile(t *testing.T, path string, done <-chan error) {
	t.Helper()
	deadline := time.After(5 * time.Second)
	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()
	for {
		if _, err := os.Stat(path); err == nil {
			return
		}
		select {
		case err := <-done:
			t.Fatalf("supervisor exited while waiting for %s: %v", filepath.Base(path), err)
		case <-deadline:
			t.Fatalf("timed out waiting for %s", filepath.Base(path))
		case <-ticker.C:
		}
	}
}
