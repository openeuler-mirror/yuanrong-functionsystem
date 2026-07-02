package service

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"strings"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	pb "runtime-launcher/api/proto/runtime/v1"
	rt "runtime-launcher/internal/runtime"
	"runtime-launcher/internal/state"
)

// LauncherService implements sandboxd-compatible runtime.v1.SandboxService.
type LauncherService struct {
	pb.UnimplementedSandboxServiceServer

	runtime  rt.ContainerRuntime
	stateMgr *state.Manager
}

func NewLauncherService(runtime rt.ContainerRuntime, stateMgr *state.Manager) *LauncherService {
	return &LauncherService{runtime: runtime, stateMgr: stateMgr}
}

func (s *LauncherService) Start(ctx context.Context, req *pb.SandboxStartRequest) (*pb.StartResponse, error) {
	if req == nil {
		return &pb.StartResponse{Code: 1, Message: "sandbox request cannot be nil"}, nil
	}
	cfg := buildCreateConfig(req)
	log.Printf("[service] Start: sandbox_id=%s runtime=%s image=%s", req.GetSandboxId(), req.GetRuntime(), cfg.Sandbox)
	return s.startWithConfig(ctx, cfg)
}

func (s *LauncherService) Restore(ctx context.Context, req *pb.RestoreRequest) (*pb.RestoreResponse, error) {
	_ = ctx
	_ = req
	return &pb.RestoreResponse{
		Code:    1,
		Message: "restore is not supported by runtime-launcher SandboxService backend",
	}, nil
}

func (s *LauncherService) startWithConfig(ctx context.Context, cfg *rt.CreateConfig) (*pb.StartResponse, error) {
	containerID, err := s.runtime.Create(ctx, cfg)
	if err != nil {
		log.Printf("[service] Start failed: %v", err)
		return &pb.StartResponse{Code: 1, Message: fmt.Sprintf("create sandbox failed: %v", err)}, nil
	}
	runtimeID := cfg.ID
	if runtimeID == "" {
		runtimeID = containerID
	}
	s.stateMgr.AddContainer(containerID, runtimeID, cfg)
	go s.watchContainer(containerID)
	return &pb.StartResponse{Code: 0, Id: containerID}, nil
}

func (s *LauncherService) Delete(ctx context.Context, req *pb.DeleteRequest) (*pb.DeleteResponse, error) {
	containerID := req.GetId()
	if containerID == "" {
		return &pb.DeleteResponse{}, fmt.Errorf("sandbox id cannot be empty")
	}
	log.Printf("[service] Delete: id=%s timeout=%d", containerID, req.GetTimeout())
	if err := s.runtime.Delete(ctx, containerID, req.GetTimeout()); err != nil {
		log.Printf("[service] Delete failed: %v", err)
	}
	s.stateMgr.RemoveContainer(containerID)
	return &pb.DeleteResponse{}, nil
}

func (s *LauncherService) Wait(ctx context.Context, req *pb.WaitRequest) (*pb.WaitResponse, error) {
	containerID := req.GetId()
	if containerID == "" {
		return &pb.WaitResponse{Status: 1, Message: "sandbox id cannot be empty"}, nil
	}
	cs, ok := s.stateMgr.GetContainer(containerID)
	if !ok {
		status, err := s.runtime.Wait(ctx, containerID)
		if err != nil {
			return &pb.WaitResponse{Status: 1, Message: err.Error()}, nil
		}
		return &pb.WaitResponse{Status: status.StatusCode, ExitCode: status.ExitCode, Message: status.Message}, nil
	}
	select {
	case <-cs.DoneCh:
		if latest, ok := s.stateMgr.GetContainer(containerID); ok {
			return &pb.WaitResponse{Status: 0, ExitCode: latest.ExitCode, Message: latest.ExitMessage}, nil
		}
		return &pb.WaitResponse{Status: 0, ExitCode: cs.ExitCode, Message: cs.ExitMessage}, nil
	case <-ctx.Done():
		return &pb.WaitResponse{Status: 1, Message: "wait timeout or canceled"}, nil
	}
}

func (s *LauncherService) List(ctx context.Context, req *pb.ListSandboxesRequest) (*pb.ListSandboxesResponse, error) {
	infos, err := s.runtime.List(ctx, req.GetId())
	if err != nil {
		log.Printf("[service] List: err=%v", err)
		return nil, status.Errorf(codes.Internal, "list sandboxes failed: %v", err)
	}
	statuses := make([]*pb.SandboxStatus, 0, len(infos))
	for _, info := range infos {
		status := containerInfoToSandboxStatus(info, s.stateMgr)
		if !matchesSelector(status.GetLabels(), req.GetSelector()) {
			continue
		}
		statuses = append(statuses, status)
	}
	return &pb.ListSandboxesResponse{Sandboxes: statuses}, nil
}

func (s *LauncherService) Stats(ctx context.Context, req *pb.StatsRequest) (*pb.StatsResponse, error) {
	id := req.GetId()
	cs, err := s.runtime.Stats(ctx, id)
	if err != nil {
		log.Printf("[service] Stats: id=%s err=%v", id, err)
		return &pb.StatsResponse{}, nil
	}
	return &pb.StatsResponse{
		CpuUsageNs:          cs.CPUUsageNs,
		MemoryUsageBytes:    cs.MemoryUsageBytes,
		MemoryLimitBytes:    cs.MemoryLimitBytes,
		MemoryMaxUsageBytes: cs.MemoryMaxUsageBytes,
	}, nil
}

func (s *LauncherService) Register(ctx context.Context, req *pb.SandboxRegisterRequest) (*pb.SandboxNormalResponse, error) {
	_ = ctx
	for _, tmpl := range req.GetTemplates() {
		s.stateMgr.RegisterTemplate(tmpl)
		log.Printf("[service] registered template: id=%s runtime=%s makeSeed=%v", tmpl.GetId(), tmpl.GetRuntime(), tmpl.GetMakeSeed())
	}
	return &pb.SandboxNormalResponse{Success: true, Message: "registered"}, nil
}

func (s *LauncherService) Unregister(ctx context.Context, req *pb.SandboxUnregisterRequest) (*pb.SandboxNormalResponse, error) {
	_ = ctx
	for _, id := range req.GetIds() {
		s.stateMgr.UnregisterTemplate(id)
	}
	return &pb.SandboxNormalResponse{Success: true, Message: "unregistered"}, nil
}

func (s *LauncherService) GetRegistered(ctx context.Context, req *pb.SandboxGetRegisteredRequest) (*pb.SandboxGetRegisteredResponse, error) {
	_ = ctx
	_ = req
	return &pb.SandboxGetRegisteredResponse{Templates: s.stateMgr.GetAllRegisteredTemplates()}, nil
}

func (s *LauncherService) Checkpoint(ctx context.Context, req *pb.SandboxCheckpointRequest) (*pb.SandboxCheckpointResponse, error) {
	_ = ctx
	_ = req
	return &pb.SandboxCheckpointResponse{
		Success: false,
		Message: "checkpoint is not supported by runtime-launcher SandboxService backend",
	}, nil
}

func buildCreateConfig(req *pb.SandboxStartRequest) *rt.CreateConfig {
	id := req.GetSandboxId()
	if id == "" {
		id = req.GetEnvs()["YR_RUNTIME_ID"]
	}
	rootfs := convertRootfs(req.GetRootfs())
	sandbox := req.GetRuntime()
	if rootfs.Type == rt.RootfsSrcImage && rootfs.ImageURL != "" {
		sandbox = rootfs.ImageURL
	}
	cpuMilli := 500.0
	memMB := 512.0
	if v, ok := req.GetResources()["CPU"]; ok {
		cpuMilli = v
	}
	if v, ok := req.GetResources()["Memory"]; ok {
		memMB = v
	}
	network := normalizeNetwork(req.GetNetwork())
	return &rt.CreateConfig{
		ID:           id,
		Sandbox:      sandbox,
		Rootfs:       rootfs,
		Command:      req.GetCommand(),
		Envs:         cloneMap(req.GetEnvs()),
		Mounts:       convertProtoMounts(req.GetMounts()),
		CPUMillicore: cpuMilli,
		MemoryMB:     memMB,
		Stdout:       req.GetStdout(),
		Stderr:       req.GetStderr(),
		ExtraConfig:  req.GetExtraConfig(),
		Network:      network,
		Ports:        req.GetPorts(),
		Labels:       cloneMap(req.GetLabels()),
	}
}

func normalizeNetwork(network string) string {
	network = strings.TrimSpace(network)
	if network == "" || network == "sandbox" {
		return "bridge"
	}
	if strings.HasPrefix(network, "{") {
		var payload map[string]any
		if err := json.Unmarshal([]byte(network), &payload); err == nil {
			if mode, ok := payload["mode"].(string); ok && strings.TrimSpace(mode) != "" {
				return strings.TrimSpace(mode)
			}
			// sandboxd callers pass portForwardings as JSON in network while
			// the actual publish rules are carried in SandboxStartRequest.ports.
			// Docker expects NetworkMode to be a real mode/name, not the JSON blob.
			return "bridge"
		}
	}
	return network
}

func convertRootfs(protoRootfs *pb.RootfsConfig) rt.RootfsConfig {
	if protoRootfs == nil {
		return rt.RootfsConfig{}
	}
	rootfs := rt.RootfsConfig{Readonly: protoRootfs.GetReadonly(), Type: rt.RootfsSrcType(protoRootfs.GetType())}
	rootfs.ImageURL = protoRootfs.GetImageUrl()
	rootfs.S3 = convertS3(protoRootfs.GetS3Config())
	return rootfs
}

func convertS3(s3 *pb.S3Config) *rt.S3Config {
	if s3 == nil {
		return nil
	}
	return &rt.S3Config{
		Endpoint:        s3.GetEndpoint(),
		Bucket:          s3.GetBucket(),
		Object:          s3.GetObject(),
		AccessKeyID:     s3.GetAccessKeyID(),
		AccessKeySecret: s3.GetAccessKeySecret(),
	}
}

func cloneMap(in map[string]string) map[string]string {
	out := make(map[string]string, len(in))
	for k, v := range in {
		out[k] = v
	}
	return out
}

func convertProtoMounts(protoMounts []*pb.Mount) []rt.MountConfig {
	mounts := make([]rt.MountConfig, 0, len(protoMounts))
	for _, m := range protoMounts {
		mounts = append(mounts, rt.MountConfig{
			Type:     m.GetType(),
			Target:   m.GetTarget(),
			Options:  m.GetOptions(),
			HostPath: m.GetHostPath(),
			S3:       convertS3(m.GetS3Config()),
			ImageURL: m.GetImageUrl(),
		})
	}
	return mounts
}

func containerInfoToSandboxStatus(info *rt.ContainerInfo, stateMgr *state.Manager) *pb.SandboxStatus {
	labels := sandboxLabels(info)
	if cs, ok := stateMgr.GetContainer(info.ID); ok {
		return containerStateToSandboxStatus(cs, labels)
	}
	status := pb.SandboxState_SANDBOX_STATE_UNKNOWN
	switch info.State {
	case "running":
		status = pb.SandboxState_SANDBOX_STATE_RUNNING
	case "exited", "dead":
		status = pb.SandboxState_SANDBOX_STATE_EXITED
	}
	return &pb.SandboxStatus{
		Id:         info.ID,
		Command:    append([]string(nil), info.Command...),
		Runtime:    info.Image,
		State:      status,
		StartedAt:  info.StartedAt,
		FinishedAt: info.FinishedAt,
		ExitCode:   info.ExitCode,
		Message:    info.Message,
		Labels:     labels,
	}
}

func sandboxLabels(info *rt.ContainerInfo) map[string]string {
	labels := make(map[string]string, len(info.Labels)+2)
	for k, v := range info.Labels {
		labels[k] = v
	}
	if info.RuntimeID != "" {
		labels[rt.RuntimeIDLabelKey] = info.RuntimeID
		// Keep the historical helper label for humans/scripts while selectors can
		// also use the backend-authoritative yr.runtime-id label.
		labels["runtime_id"] = info.RuntimeID
	}
	return labels
}

func containerStateToSandboxStatus(cs *state.ContainerState, labels map[string]string) *pb.SandboxStatus {
	status := pb.SandboxState_SANDBOX_STATE_RUNNING
	if cs.Exited {
		status = pb.SandboxState_SANDBOX_STATE_EXITED
	}
	cfg := cs.Config
	sandboxStatus := &pb.SandboxStatus{
		Id:         cs.ID,
		State:      status,
		StartedAt:  cs.StartedAt,
		FinishedAt: cs.FinishedAt,
		ExitCode:   cs.ExitCode,
		Message:    cs.ExitMessage,
		Labels:     labels,
	}
	if cfg != nil {
		sandboxStatus.Command = append([]string(nil), cfg.Command...)
		sandboxStatus.Runtime = cfg.Sandbox
		sandboxStatus.Mounts = runtimeMountsToProto(cfg.Mounts)
		sandboxStatus.Envs = envMapToKeyValues(cfg.Envs)
		sandboxStatus.Stdout = cfg.Stdout
		sandboxStatus.Stderr = cfg.Stderr
		sandboxStatus.Resources = &pb.LinuxSandboxResources{
			MemoryLimitInBytes: int64(cfg.MemoryMB * 1024 * 1024),
		}
		if cfg.CPUMillicore > 0 {
			sandboxStatus.Resources.CpuPeriod = 100000
			sandboxStatus.Resources.CpuQuota = int64(cfg.CPUMillicore * 100)
		}
	}
	return sandboxStatus
}

func runtimeMountsToProto(mounts []rt.MountConfig) []*pb.Mount {
	result := make([]*pb.Mount, 0, len(mounts))
	for _, m := range mounts {
		pm := &pb.Mount{Type: m.Type, Target: m.Target, Options: append([]string(nil), m.Options...)}
		if m.HostPath != "" {
			pm.Source = &pb.Mount_HostPath{HostPath: m.HostPath}
		} else if m.ImageURL != "" {
			pm.Source = &pb.Mount_ImageUrl{ImageUrl: m.ImageURL}
		} else if m.S3 != nil {
			pm.Source = &pb.Mount_S3Config{S3Config: &pb.S3Config{
				Endpoint:        m.S3.Endpoint,
				Bucket:          m.S3.Bucket,
				Object:          m.S3.Object,
				AccessKeyID:     m.S3.AccessKeyID,
				AccessKeySecret: m.S3.AccessKeySecret,
			}}
		}
		result = append(result, pm)
	}
	return result
}

func envMapToKeyValues(envs map[string]string) []*pb.KeyValue {
	result := make([]*pb.KeyValue, 0, len(envs))
	for k, v := range envs {
		result = append(result, &pb.KeyValue{Key: k, Value: v})
	}
	return result
}

func matchesSelector(labels, selector map[string]string) bool {
	for k, v := range selector {
		if labels[k] != v {
			return false
		}
	}
	return true
}

func (s *LauncherService) watchContainer(containerID string) {
	status, err := s.runtime.Wait(context.Background(), containerID)
	if err != nil {
		log.Printf("[service] watch sandbox %s failed: %v", containerID, err)
		s.stateMgr.MarkExited(containerID, 1, err.Error())
		return
	}
	s.stateMgr.MarkExited(containerID, status.ExitCode, status.Message)
}
