// Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package service

import (
	"context"
	"errors"
	"strings"
	"testing"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/reflect/protoreflect"

	"runtime-launcher/api/proto/runtime/v1"
	"runtime-launcher/internal/runtime"
	"runtime-launcher/internal/state"
)

const (
	sandboxIDFieldNumber    protoreflect.FieldNumber = 1
	templateIDFieldNumber   protoreflect.FieldNumber = 2
	runtimeFieldNumber      protoreflect.FieldNumber = 3
	rootfsFieldNumber       protoreflect.FieldNumber = 4
	commandFieldNumber      protoreflect.FieldNumber = 5
	portsFieldNumber        protoreflect.FieldNumber = 15
	labelsFieldNumber       protoreflect.FieldNumber = 16
	metricLabelsFieldNumber protoreflect.FieldNumber = 17
	sandboxIPFieldNumber    protoreflect.FieldNumber = 4
	waitExitCode                                     = 7
)

type fakeRuntime struct {
	listInfos    []*runtime.ContainerInfo
	listErr      error
	createID     string
	createErr    error
	createdCfg   *runtime.CreateConfig
	endpoint     *runtime.NetworkEndpoint
	endpointErr  error
	resolveCalls int
	deletedIDs   []string
	deleteErr    error
}

func (f *fakeRuntime) Name() string { return "fake" }
func (f *fakeRuntime) Create(_ context.Context, cfg *runtime.CreateConfig) (string, error) {
	if cfg != nil {
		copyCfg := *cfg
		f.createdCfg = &copyCfg
	}
	return f.createID, f.createErr
}
func (f *fakeRuntime) ResolveEndpoint(context.Context, string) (*runtime.NetworkEndpoint, error) {
	f.resolveCalls++
	if f.endpoint == nil {
		return nil, f.endpointErr
	}
	endpoint := *f.endpoint
	return &endpoint, f.endpointErr
}
func (f *fakeRuntime) Wait(context.Context, string) (*runtime.ContainerStatus, error) {
	return &runtime.ContainerStatus{}, nil
}
func (f *fakeRuntime) Delete(_ context.Context, id string, _ int64) error {
	f.deletedIDs = append(f.deletedIDs, id)
	return f.deleteErr
}
func (f *fakeRuntime) Stats(context.Context, string) (*runtime.ContainerStats, error) {
	return nil, errors.New("not implemented")
}
func (f *fakeRuntime) List(context.Context, string) ([]*runtime.ContainerInfo, error) {
	return f.listInfos, f.listErr
}
func (f *fakeRuntime) Close() error { return nil }

func TestStartRequestFieldNumbersMatchSandboxAPI(t *testing.T) {
	fields := (&runtimev1.StartRequest{}).ProtoReflect().Descriptor().Fields()
	assertFieldNumber(t, fields, "sandbox_id", sandboxIDFieldNumber)
	assertFieldNumber(t, fields, "template_id", templateIDFieldNumber)
	assertFieldNumber(t, fields, "runtime", runtimeFieldNumber)
	assertFieldNumber(t, fields, "rootfs", rootfsFieldNumber)
	assertFieldNumber(t, fields, "command", commandFieldNumber)
	assertFieldNumber(t, fields, "ports", portsFieldNumber)
	assertFieldNumber(t, fields, "labels", labelsFieldNumber)
	assertFieldNumber(t, fields, "metric_labels", metricLabelsFieldNumber)
}

func TestStartResponseEndpointFieldNumbersMatchSandboxAPI(t *testing.T) {
	fields := (&runtimev1.StartResponse{}).ProtoReflect().Descriptor().Fields()
	assertFieldNumber(t, fields, "sandbox_ip", sandboxIPFieldNumber)
}

func TestStartReturnsBackendSandboxEndpoint(t *testing.T) {
	fake := &fakeRuntime{createID: "container-bridge"}
	svc := NewLauncherService(fake, state.NewManager())

	fake.endpoint = &runtime.NetworkEndpoint{SandboxIP: "172.18.0.23"}

	response, err := svc.startWithConfig(context.Background(), &runtime.CreateConfig{
		ID:      "runtime-bridge",
		Network: "bridge",
	})
	if err != nil {
		t.Fatalf("Start returned error: %v", err)
	}
	if response.GetCode() != 0 || response.GetId() != "container-bridge" {
		t.Fatalf("Start response = %#v, want successful container", response)
	}
	if response.GetSandboxIp() != "172.18.0.23" {
		t.Fatalf("Start endpoint = %s, want sandbox IP", response.GetSandboxIp())
	}
}

func TestStartCleansUpContainerWhenEndpointResolveFails(t *testing.T) {
	fake := &fakeRuntime{
		createID:    "container-unresolved",
		endpointErr: errors.New("inspect failed"),
	}
	stateMgr := state.NewManager()
	svc := NewLauncherService(fake, stateMgr)

	response, err := svc.startWithConfig(context.Background(), &runtime.CreateConfig{
		ID:      "runtime-unresolved",
		Network: "bridge",
	})
	if err != nil {
		t.Fatalf("Start returned transport error: %v", err)
	}
	if response.GetCode() == 0 || !strings.Contains(response.GetMessage(), "resolve sandbox endpoint failed") {
		t.Fatalf("Start response = %#v, want endpoint failure", response)
	}
	if len(fake.deletedIDs) != 1 || fake.deletedIDs[0] != "container-unresolved" {
		t.Fatalf("deleted IDs = %#v, want failed container cleanup", fake.deletedIDs)
	}
	if _, ok := stateMgr.GetContainer("container-unresolved"); ok {
		t.Fatal("failed container was added to state manager")
	}
}

func TestStartRejectsNilEndpointResult(t *testing.T) {
	fake := &fakeRuntime{createID: "container-no-endpoint"}
	svc := NewLauncherService(fake, state.NewManager())

	response, err := svc.startWithConfig(context.Background(), &runtime.CreateConfig{
		ID:      "runtime-no-endpoint",
		Network: "bridge",
	})
	if err != nil {
		t.Fatalf("Start returned transport error: %v", err)
	}
	if response.GetCode() == 0 || !strings.Contains(response.GetMessage(), "no sandbox endpoint") {
		t.Fatalf("Start response = %#v, want missing endpoint failure", response)
	}
	if len(fake.deletedIDs) != 1 || fake.deletedIDs[0] != "container-no-endpoint" {
		t.Fatalf("deleted IDs = %#v, want failed container cleanup", fake.deletedIDs)
	}
}

func TestStartHostNetworkDoesNotPublishSandboxEndpoint(t *testing.T) {
	fake := &fakeRuntime{createID: "container-host"}
	svc := NewLauncherService(fake, state.NewManager())

	response, err := svc.startWithConfig(context.Background(), &runtime.CreateConfig{
		ID:      "runtime-host",
		Network: "host",
	})
	if err != nil || response.GetCode() != 0 {
		t.Fatalf("Start host response=%#v err=%v, want success", response, err)
	}
	if response.GetSandboxIp() != "" {
		t.Fatalf("host endpoint = %s, want empty", response.GetSandboxIp())
	}
	if fake.resolveCalls != 0 {
		t.Fatalf("host resolve calls=%d, want zero", fake.resolveCalls)
	}
}

func TestNetworkHasIsolatedEndpoint(t *testing.T) {
	tests := map[string]bool{
		"bridge":       true,
		"yr-sandboxes": true,
		"default":      true,
		"host":         false,
		"none":         false,
		"container:x":  false,
	}
	for network, want := range tests {
		if got := networkHasIsolatedEndpoint(network); got != want {
			t.Errorf("networkHasIsolatedEndpoint(%q) = %v, want %v", network, got, want)
		}
	}
}

func assertFieldNumber(t *testing.T, fields protoreflect.FieldDescriptors, name string, want protoreflect.FieldNumber) {
	t.Helper()
	field := fields.ByName(protoreflect.Name(name))
	if field == nil {
		t.Fatalf("protobuf message missing field %q", name)
	}
	if got := field.Number(); got != want {
		t.Fatalf("protobuf field %s number = %d, want %d", name, got, want)
	}
}

func TestListPropagatesBackendError(t *testing.T) {
	svc := NewLauncherService(&fakeRuntime{listErr: errors.New("docker down")}, state.NewManager())
	_, err := svc.List(context.Background(), &runtimev1.ListSandboxesRequest{})
	if err == nil {
		t.Fatalf("List returned nil error for backend failure")
	}
	if status.Code(err) != codes.Internal {
		t.Fatalf("List error code = %v, want %v", status.Code(err), codes.Internal)
	}
}

func TestCheckpointReturnsUnimplementedStatus(t *testing.T) {
	svc := NewLauncherService(&fakeRuntime{}, state.NewManager())
	response, err := svc.Checkpoint(context.Background(), &runtimev1.CheckpointRequest{})
	if response != nil {
		t.Fatalf("Checkpoint response = %#v, want nil", response)
	}
	if status.Code(err) != codes.Unimplemented {
		t.Fatalf("Checkpoint error code = %v, want %v", status.Code(err), codes.Unimplemented)
	}
}

func TestListAvailableRuntimesReturnsSuccessfulEmptySnapshot(t *testing.T) {
	svc := NewLauncherService(&fakeRuntime{}, state.NewManager())
	resp, err := svc.ListAvailableRuntimes(context.Background(), &runtimev1.ListAvailableRuntimesRequest{})
	if err != nil {
		t.Fatalf("ListAvailableRuntimes returned error: %v", err)
	}
	if len(resp.GetRuntimeClasses()) != 0 {
		t.Fatalf("ListAvailableRuntimes returned %v, want empty snapshot", resp.GetRuntimeClasses())
	}
}

func TestListSelectorUsesBackendLabelsForTrackedContainers(t *testing.T) {
	stateMgr := state.NewManager()
	stateMgr.AddContainer("container-1", "runtime-1", &runtime.CreateConfig{ID: "runtime-1", Sandbox: "image:latest"})
	svc := NewLauncherService(&fakeRuntime{listInfos: []*runtime.ContainerInfo{
		{
			ID:        "container-1",
			RuntimeID: "runtime-1",
			Image:     "image:latest",
			State:     "running",
			Labels: map[string]string{
				runtime.ManagedLabelKey:   runtime.ManagedLabelValue,
				runtime.RuntimeIDLabelKey: "runtime-1",
			},
		},
	}}, stateMgr)

	resp, err := svc.List(context.Background(), &runtimev1.ListSandboxesRequest{Selector: map[string]string{
		runtime.ManagedLabelKey:   runtime.ManagedLabelValue,
		runtime.RuntimeIDLabelKey: "runtime-1",
	}})
	if err != nil {
		t.Fatalf("List returned error: %v", err)
	}
	if got := len(resp.GetSandboxes()); got != 1 {
		t.Fatalf("List returned %d sandboxes, want 1", got)
	}
	labels := resp.GetSandboxes()[0].GetLabels()
	if labels[runtime.ManagedLabelKey] != runtime.ManagedLabelValue || labels[runtime.RuntimeIDLabelKey] != "runtime-1" {
		t.Fatalf("labels = %#v, want backend labels preserved", labels)
	}
}

func TestWaitReloadsExitStatusAfterDone(t *testing.T) {
	stateMgr := state.NewManager()
	stateMgr.AddContainer("container-wait", "runtime-wait", &runtime.CreateConfig{ID: "runtime-wait"})
	svc := NewLauncherService(&fakeRuntime{}, stateMgr)

	go stateMgr.MarkExited("container-wait", waitExitCode, "boom")
	resp, err := svc.Wait(context.Background(), &runtimev1.WaitRequest{Id: "container-wait"})
	if err != nil {
		t.Fatalf("Wait returned error: %v", err)
	}
	if resp.GetExitCode() != waitExitCode || resp.GetMessage() != "boom" {
		t.Fatalf(
			"Wait response = exit_code=%d message=%q, want exit_code=%d message=boom",
			resp.GetExitCode(),
			resp.GetMessage(),
			waitExitCode,
		)
	}
}

func TestBuildCreateConfigNormalizesSandboxdNetworkJSON(t *testing.T) {
	cfg := buildCreateConfig(&runtimev1.StartRequest{
		Network: `{"portForwardings":[{"port":8080,"protocol":"TCP"}]}`,
		Ports:   []string{"tcp:20000:8080"},
	})
	if cfg.Network != "bridge" {
		t.Fatalf("Network = %q, want bridge", cfg.Network)
	}
	if len(cfg.Ports) != 1 || cfg.Ports[0] != "tcp:20000:8080" {
		t.Fatalf("Ports = %#v, want preserved port mapping", cfg.Ports)
	}
}

func TestBuildCreateConfigUsesNetworkModeFromJSONWhenPresent(t *testing.T) {
	cfg := buildCreateConfig(&runtimev1.StartRequest{Network: `{"mode":"host","portForwardings":[{"port":8080}]}`})
	if cfg.Network != "host" {
		t.Fatalf("Network = %q, want host", cfg.Network)
	}
}

func TestBuildCreateConfigFallsBackForInvalidNetworkJSON(t *testing.T) {
	cfg := buildCreateConfig(&runtimev1.StartRequest{Network: `{"mode":`})
	if cfg.Network != "bridge" {
		t.Fatalf("Network = %q, want bridge", cfg.Network)
	}
}

func TestBuildCreateConfigUsesRuntimeIDEnvWhenSandboxIDEmpty(t *testing.T) {
	cfg := buildCreateConfig(&runtimev1.StartRequest{Envs: map[string]string{"YR_RUNTIME_ID": "runtime-123"}})
	if cfg.ID != "runtime-123" {
		t.Fatalf("ID = %q, want runtime-123", cfg.ID)
	}
}

func TestBuildCreateConfigPreservesSandboxLabels(t *testing.T) {
	cfg := buildCreateConfig(&runtimev1.StartRequest{Labels: map[string]string{"app": "demo"}})
	if cfg.Labels["app"] != "demo" {
		t.Fatalf("Labels = %#v, want app=demo", cfg.Labels)
	}
}

func TestServiceBuildCreateConfigMergesRegisteredTemplateEnvs(t *testing.T) {
	stateMgr := state.NewManager()
	stateMgr.RegisterTemplate(&runtimev1.SandboxTemplate{
		Id: "template-1",
		Envs: map[string]string{
			"BASE_ENV":   "from-template",
			"SHARED_ENV": "from-template",
		},
	})
	svc := NewLauncherService(&fakeRuntime{}, stateMgr)

	cfg := svc.buildCreateConfig(&runtimev1.StartRequest{
		TemplateId: "template-1",
		Envs: map[string]string{
			"USER_ENV":   "from-request",
			"SHARED_ENV": "from-request",
		},
	})

	if cfg.Envs["BASE_ENV"] != "from-template" {
		t.Fatalf("BASE_ENV = %q, want from-template", cfg.Envs["BASE_ENV"])
	}
	if cfg.Envs["USER_ENV"] != "from-request" {
		t.Fatalf("USER_ENV = %q, want from-request", cfg.Envs["USER_ENV"])
	}
	if cfg.Envs["SHARED_ENV"] != "from-request" {
		t.Fatalf("SHARED_ENV = %q, want request env to override template env", cfg.Envs["SHARED_ENV"])
	}
}
