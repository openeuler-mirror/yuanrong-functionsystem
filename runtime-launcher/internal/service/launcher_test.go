package service

import (
	"context"
	"errors"
	"testing"

	"google.golang.org/protobuf/reflect/protoreflect"

	pb "runtime-launcher/api/proto/runtime/v1"
	rt "runtime-launcher/internal/runtime"
	"runtime-launcher/internal/state"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

type fakeRuntime struct {
	listInfos []*rt.ContainerInfo
	listErr   error
}

func (f *fakeRuntime) Name() string { return "fake" }
func (f *fakeRuntime) Create(context.Context, *rt.CreateConfig) (string, error) {
	return "", errors.New("not implemented")
}
func (f *fakeRuntime) Wait(context.Context, string) (*rt.ContainerStatus, error) {
	return nil, errors.New("not implemented")
}
func (f *fakeRuntime) Delete(context.Context, string, int64) error { return nil }
func (f *fakeRuntime) Stats(context.Context, string) (*rt.ContainerStats, error) {
	return nil, errors.New("not implemented")
}
func (f *fakeRuntime) List(context.Context, string) ([]*rt.ContainerInfo, error) {
	return f.listInfos, f.listErr
}
func (f *fakeRuntime) Close() error { return nil }

func TestSandboxStartRequestFieldNumbersMatchSandboxAPI(t *testing.T) {
	fields := (&pb.SandboxStartRequest{}).ProtoReflect().Descriptor().Fields()
	assertFieldNumber(t, fields, "sandbox_id", 1)
	assertFieldNumber(t, fields, "template_id", 2)
	assertFieldNumber(t, fields, "runtime", 3)
	assertFieldNumber(t, fields, "rootfs", 4)
	assertFieldNumber(t, fields, "command", 5)
	assertFieldNumber(t, fields, "ports", 15)
	assertFieldNumber(t, fields, "labels", 16)
	assertFieldNumber(t, fields, "metric_labels", 17)
}

func assertFieldNumber(t *testing.T, fields protoreflect.FieldDescriptors, name string, want protoreflect.FieldNumber) {
	t.Helper()
	field := fields.ByName(protoreflect.Name(name))
	if field == nil {
		t.Fatalf("SandboxStartRequest missing field %q", name)
	}
	if got := field.Number(); got != want {
		t.Fatalf("SandboxStartRequest.%s field number = %d, want %d", name, got, want)
	}
}

func TestListPropagatesBackendError(t *testing.T) {
	svc := NewLauncherService(&fakeRuntime{listErr: errors.New("docker down")}, state.NewManager())
	_, err := svc.List(context.Background(), &pb.ListSandboxesRequest{})
	if err == nil {
		t.Fatalf("List returned nil error for backend failure")
	}
	if status.Code(err) != codes.Internal {
		t.Fatalf("List error code = %v, want %v", status.Code(err), codes.Internal)
	}
}

func TestListSelectorUsesBackendLabelsForTrackedContainers(t *testing.T) {
	stateMgr := state.NewManager()
	stateMgr.AddContainer("container-1", "runtime-1", &rt.CreateConfig{ID: "runtime-1", Sandbox: "image:latest"})
	svc := NewLauncherService(&fakeRuntime{listInfos: []*rt.ContainerInfo{
		{
			ID:        "container-1",
			RuntimeID: "runtime-1",
			Image:     "image:latest",
			State:     "running",
			Labels: map[string]string{
				rt.ManagedLabelKey:   rt.ManagedLabelValue,
				rt.RuntimeIDLabelKey: "runtime-1",
			},
		},
	}}, stateMgr)

	resp, err := svc.List(context.Background(), &pb.ListSandboxesRequest{Selector: map[string]string{
		rt.ManagedLabelKey:   rt.ManagedLabelValue,
		rt.RuntimeIDLabelKey: "runtime-1",
	}})
	if err != nil {
		t.Fatalf("List returned error: %v", err)
	}
	if got := len(resp.GetSandboxes()); got != 1 {
		t.Fatalf("List returned %d sandboxes, want 1", got)
	}
	labels := resp.GetSandboxes()[0].GetLabels()
	if labels[rt.ManagedLabelKey] != rt.ManagedLabelValue || labels[rt.RuntimeIDLabelKey] != "runtime-1" {
		t.Fatalf("labels = %#v, want backend labels preserved", labels)
	}
}

func TestWaitReloadsExitStatusAfterDone(t *testing.T) {
	stateMgr := state.NewManager()
	stateMgr.AddContainer("container-wait", "runtime-wait", &rt.CreateConfig{ID: "runtime-wait"})
	svc := NewLauncherService(&fakeRuntime{}, stateMgr)

	go stateMgr.MarkExited("container-wait", 7, "boom")
	resp, err := svc.Wait(context.Background(), &pb.WaitRequest{Id: "container-wait"})
	if err != nil {
		t.Fatalf("Wait returned error: %v", err)
	}
	if resp.GetExitCode() != 7 || resp.GetMessage() != "boom" {
		t.Fatalf("Wait response = exit_code=%d message=%q, want exit_code=7 message=boom", resp.GetExitCode(), resp.GetMessage())
	}
}

func TestBuildCreateConfigNormalizesSandboxdNetworkJSON(t *testing.T) {
	cfg := buildCreateConfig(&pb.SandboxStartRequest{
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
	cfg := buildCreateConfig(&pb.SandboxStartRequest{Network: `{"mode":"host","portForwardings":[{"port":8080}]}`})
	if cfg.Network != "host" {
		t.Fatalf("Network = %q, want host", cfg.Network)
	}
}

func TestBuildCreateConfigUsesRuntimeIDEnvWhenSandboxIDEmpty(t *testing.T) {
	cfg := buildCreateConfig(&pb.SandboxStartRequest{Envs: map[string]string{"YR_RUNTIME_ID": "runtime-123"}})
	if cfg.ID != "runtime-123" {
		t.Fatalf("ID = %q, want runtime-123", cfg.ID)
	}
}

func TestBuildCreateConfigPreservesSandboxLabels(t *testing.T) {
	cfg := buildCreateConfig(&pb.SandboxStartRequest{Labels: map[string]string{"app": "demo"}})
	if cfg.Labels["app"] != "demo" {
		t.Fatalf("Labels = %#v, want app=demo", cfg.Labels)
	}
}
