package service

import (
	"context"
	"errors"
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
	waitExitCode                                     = 7
)

type fakeRuntime struct {
	listInfos []*runtime.ContainerInfo
	listErr   error
}

func (f *fakeRuntime) Name() string { return "fake" }
func (f *fakeRuntime) Create(context.Context, *runtime.CreateConfig) (string, error) {
	return "", errors.New("not implemented")
}
func (f *fakeRuntime) Wait(context.Context, string) (*runtime.ContainerStatus, error) {
	return nil, errors.New("not implemented")
}
func (f *fakeRuntime) Delete(context.Context, string, int64) error { return nil }
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

func assertFieldNumber(t *testing.T, fields protoreflect.FieldDescriptors, name string, want protoreflect.FieldNumber) {
	t.Helper()
	field := fields.ByName(protoreflect.Name(name))
	if field == nil {
		t.Fatalf("StartRequest missing field %q", name)
	}
	if got := field.Number(); got != want {
		t.Fatalf("StartRequest.%s field number = %d, want %d", name, got, want)
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
