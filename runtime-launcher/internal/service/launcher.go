package service

import (
	"context"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	pb "runtime-launcher/api/proto/runtime/v1"
	rt "runtime-launcher/internal/runtime"
	"runtime-launcher/internal/state"
)

// LauncherService 实现 RuntimeLauncher gRPC 服务的 8 个 RPC 方法。
type LauncherService struct {
	pb.UnimplementedRuntimeLauncherServer

	runtime  rt.ContainerRuntime
	stateMgr *state.Manager
}

// NewLauncherService 创建 LauncherService 实例。
func NewLauncherService(runtime rt.ContainerRuntime, stateMgr *state.Manager) *LauncherService {
	return &LauncherService{
		runtime:  runtime,
		stateMgr: stateMgr,
	}
}

// Start 创建并启动一个容器。
func (s *LauncherService) Start(ctx context.Context, req *pb.StartRequest) (*pb.StartResponse, error) {
	if req.GetFuncRuntime() == nil {
		return &pb.StartResponse{Code: 1, Message: "funcRuntime 不能为空"}, nil
	}

	funcRt := req.GetFuncRuntime()
	runtimeID := funcRt.GetId()

	// 构建 CreateConfig
	cfg := s.buildCreateConfig(req)

	// 如果此 runtimeID 已注册（预热），合并注册时的环境变量
	if regRt, ok := s.stateMgr.GetRegisteredRuntime(runtimeID); ok {
		for k, v := range regRt.GetRuntimeEnvs() {
			if _, exists := cfg.Envs[k]; !exists {
				cfg.Envs[k] = v
			}
		}
	}

	log.Printf("[service] Start: runtimeID=%s, sandbox=%s", runtimeID, cfg.Sandbox)

	// 调用运行时后端创建容器
	containerID, err := s.runtime.Create(ctx, cfg)
	if err != nil {
		log.Printf("[service] Start 失败: %v", err)
		return &pb.StartResponse{
			Code:    1,
			Message: fmt.Sprintf("创建容器失败: %v", err),
		}, nil
	}

	// 在状态管理器中记录容器
	s.stateMgr.AddContainer(containerID, runtimeID)

	// 启动后台 goroutine 监听容器退出
	go s.watchContainer(containerID)

	return &pb.StartResponse{
		Code: 0,
		Id:   containerID,
	}, nil
}

// Wait 阻塞直到容器退出，返回退出状态。
func (s *LauncherService) Wait(ctx context.Context, req *pb.WaitRequest) (*pb.WaitResponse, error) {
	containerID := req.GetId()
	if containerID == "" {
		return &pb.WaitResponse{Status: 1, Message: "容器 ID 不能为空"}, nil
	}

	// 检查容器是否存在于状态管理器
	cs, ok := s.stateMgr.GetContainer(containerID)
	if !ok {
		// 容器不在状态管理器中，直接调用运行时 Wait
		status, err := s.runtime.Wait(ctx, containerID)
		if err != nil {
			return &pb.WaitResponse{Status: 1, Message: err.Error()}, nil
		}
		return &pb.WaitResponse{
			Status:   status.StatusCode,
			ExitCode: status.ExitCode,
			Message:  status.Message,
		}, nil
	}

	// 等待容器退出（通过 DoneCh）
	select {
	case <-cs.DoneCh:
		return &pb.WaitResponse{
			Status:   0,
			ExitCode: cs.ExitCode,
			Message:  cs.ExitMessage,
		}, nil
	case <-ctx.Done():
		return &pb.WaitResponse{Status: 1, Message: "等待超时或被取消"}, nil
	}
}

// Delete 停止并删除容器。
func (s *LauncherService) Delete(ctx context.Context, req *pb.DeleteRequest) (*pb.DeleteResponse, error) {
	containerID := req.GetId()
	if containerID == "" {
		return &pb.DeleteResponse{}, fmt.Errorf("容器 ID 不能为空")
	}

	log.Printf("[service] Delete: containerID=%s, timeout=%d", containerID, req.GetTimeout())

	err := s.runtime.Delete(ctx, containerID, req.GetTimeout())
	if err != nil {
		log.Printf("[service] Delete 失败: %v", err)
		// 仍然从状态中清理
	}

	s.stateMgr.RemoveContainer(containerID)

	return &pb.DeleteResponse{}, nil
}

// Register 将 FunctionRuntime 列表注册到预热注册表。
func (s *LauncherService) Register(ctx context.Context, req *pb.RegisterRequest) (*pb.NormalResponse, error) {
	runtimes := req.GetFuncRuntimes()
	log.Printf("[service] Register: 注册 %d 个运行时", len(runtimes))

	for _, funcRt := range runtimes {
		s.stateMgr.RegisterRuntime(funcRt)
		log.Printf("[service] 已注册运行时: id=%s, sandbox=%s, makeSeed=%v",
			funcRt.GetId(), funcRt.GetSandbox(), funcRt.GetMakeSeed())
	}

	return &pb.NormalResponse{Success: true, Message: "注册成功"}, nil
}

// Unregister 从预热注册表中移除指定 ID 的运行时。
func (s *LauncherService) Unregister(ctx context.Context, req *pb.UnregisterRequest) (*pb.NormalResponse, error) {
	ids := req.GetIds()
	log.Printf("[service] Unregister: 注销 %d 个运行时: %s", len(ids), strings.Join(ids, ", "))

	for _, id := range ids {
		s.stateMgr.UnregisterRuntime(id)
	}

	return &pb.NormalResponse{Success: true, Message: "注销成功"}, nil
}

// GetRegistered 返回所有已注册的 FunctionRuntime。
func (s *LauncherService) GetRegistered(ctx context.Context, req *pb.GetRegisteredRequest) (*pb.GetRegisteredResponse, error) {
	registered := s.stateMgr.GetAllRegistered()
	log.Printf("[service] GetRegistered: 返回 %d 个已注册运行时", len(registered))

	return &pb.GetRegisteredResponse{
		FuncRuntimes: registered,
	}, nil
}

// buildCreateConfig 从 proto StartRequest 构建 CreateConfig。
func (s *LauncherService) buildCreateConfig(req *pb.StartRequest) *rt.CreateConfig {
	funcRt := req.GetFuncRuntime()

	// 合并环境变量：runtimeEnvs + userEnvs（userEnvs 优先）
	envs := make(map[string]string)
	for k, v := range funcRt.GetRuntimeEnvs() {
		envs[k] = v
	}
	for k, v := range req.GetUserEnvs() {
		envs[k] = v
	}

	// 转换挂载配置
	mounts := make([]rt.MountConfig, 0, len(req.GetMounts()))
	for _, m := range req.GetMounts() {
		mounts = append(mounts, rt.MountConfig{
			Type:    m.GetType(),
			Source:  m.GetSource(),
			Target:  m.GetTarget(),
			Options: m.GetOptions(),
		})
	}

	// 转换 rootfs 配置
	rootfs := rt.RootfsConfig{}
	if funcRt.GetRootfs() != nil {
		protoRootfs := funcRt.GetRootfs()
		rootfs.Readonly = protoRootfs.GetReadonly()
		rootfs.Type = rt.RootfsSrcType(protoRootfs.GetType())
		rootfs.ImageURL = protoRootfs.GetImageUrl()
		if protoRootfs.GetS3Config() != nil {
			rootfs.S3 = &rt.S3Config{
				Endpoint:        protoRootfs.GetS3Config().GetEndpoint(),
				Bucket:          protoRootfs.GetS3Config().GetBucket(),
				Object:          protoRootfs.GetS3Config().GetObject(),
				AccessKeyID:     protoRootfs.GetS3Config().GetAccessKeyID(),
				AccessKeySecret: protoRootfs.GetS3Config().GetAccessKeySecret(),
			}
		}
	}

	// 提取资源限制
	cpuMilli := 500.0  // 默认值
	memMB := 512.0     // 默认值
	if v, ok := req.GetResources()["CPU"]; ok {
		cpuMilli = v
	}
	if v, ok := req.GetResources()["Memory"]; ok {
		memMB = v
	}

	// 网络模式配置，默认使用 host
	network := req.GetNetwork()
	if network == "" {
		network = "bridge"
	}

	return &rt.CreateConfig{
		ID:           funcRt.GetId(),
		Sandbox:      funcRt.GetSandbox(),
		Rootfs:       rootfs,
		Command:      funcRt.GetCommand(),
		Envs:         envs,
		Mounts:       mounts,
		CPUMillicore: cpuMilli,
		MemoryMB:     memMB,
		Stdout:       req.GetStdout(),
		Stderr:       req.GetStderr(),
		ExtraConfig:  req.GetExtraConfig(),
		MakeSeed:     funcRt.GetMakeSeed(),
		Network:      network,
	}
}

// Checkpoint 对指定容器创建检查点（mock 实现：在 ckpt_dir 下生成标记文件）。
func (s *LauncherService) Checkpoint(ctx context.Context, req *pb.CheckpointRequest) (*pb.CheckpointResponse, error) {
	containerID := req.GetId()
	ckptDir := req.GetCkptDir()

	if containerID == "" {
		return &pb.CheckpointResponse{Success: false, Message: "容器 ID 不能为空"}, nil
	}
	if ckptDir == "" {
		return &pb.CheckpointResponse{Success: false, Message: "ckpt_dir 不能为空"}, nil
	}

	log.Printf("[service] Checkpoint(mock): containerID=%s, ckpt_dir=%s", containerID, ckptDir)

	// mock: 创建 ckpt_dir 目录，并写入标记文件
	if err := os.MkdirAll(ckptDir, 0755); err != nil {
		return &pb.CheckpointResponse{
			Success: false,
			Message: fmt.Sprintf("创建检查点目录失败: %v", err),
		}, nil
	}

	markerPath := filepath.Join(ckptDir, "checkpoint.marker")
	markerContent := fmt.Sprintf("container_id=%s\n", containerID)
	if err := os.WriteFile(markerPath, []byte(markerContent), 0644); err != nil {
		return &pb.CheckpointResponse{
			Success: false,
			Message: fmt.Sprintf("写入检查点标记文件失败: %v", err),
		}, nil
	}

	log.Printf("[service] Checkpoint(mock): 成功，marker=%s", markerPath)
	return &pb.CheckpointResponse{Success: true, Message: "checkpoint mock 成功"}, nil
}

// Restore 从检查点恢复容器（mock 实现：校验 ckpt_dir 存在后按 Start 方式启动新容器）。
func (s *LauncherService) Restore(ctx context.Context, req *pb.RestoreRequest) (*pb.RestoreResponse, error) {
	ckptDir := req.GetCkptDir()

	if req.GetFuncRuntime() == nil {
		return &pb.RestoreResponse{Code: 1, Message: "funcRuntime 不能为空"}, nil
	}
	if ckptDir == "" {
		return &pb.RestoreResponse{Code: 1, Message: "ckpt_dir 不能为空"}, nil
	}

	// 检查 ckpt_dir 是否存在
	if _, err := os.Stat(ckptDir); os.IsNotExist(err) {
		return &pb.RestoreResponse{
			Code:    1,
			Message: fmt.Sprintf("检查点目录不存在: %s", ckptDir),
		}, nil
	}

	funcRt := req.GetFuncRuntime()
	runtimeID := funcRt.GetId()

	log.Printf("[service] Restore(mock): runtimeID=%s, ckpt_dir=%s", runtimeID, ckptDir)

	// 按照 Start 的方式构建配置并启动新容器
	cfg := s.buildCreateConfigFromRestore(req)

	// 如果此 runtimeID 已注册（预热），合并注册时的环境变量
	if regRt, ok := s.stateMgr.GetRegisteredRuntime(runtimeID); ok {
		for k, v := range regRt.GetRuntimeEnvs() {
			if _, exists := cfg.Envs[k]; !exists {
				cfg.Envs[k] = v
			}
		}
	}

	containerID, err := s.runtime.Create(ctx, cfg)
	if err != nil {
		log.Printf("[service] Restore(mock) 创建容器失败: %v", err)
		return &pb.RestoreResponse{
			Code:    1,
			Message: fmt.Sprintf("创建容器失败: %v", err),
		}, nil
	}

	s.stateMgr.AddContainer(containerID, runtimeID)
	go s.watchContainer(containerID)

	log.Printf("[service] Restore(mock): 成功，containerID=%s", containerID)
	return &pb.RestoreResponse{
		Code: 0,
		Id:   containerID,
	}, nil
}

// buildCreateConfigFromRestore 从 proto RestoreRequest 构建 CreateConfig。
// RestoreRequest 与 StartRequest 结构相同（额外多 ckpt_dir 和 trace_id）。
func (s *LauncherService) buildCreateConfigFromRestore(req *pb.RestoreRequest) *rt.CreateConfig {
	funcRt := req.GetFuncRuntime()

	envs := make(map[string]string)
	for k, v := range funcRt.GetRuntimeEnvs() {
		envs[k] = v
	}
	for k, v := range req.GetUserEnvs() {
		envs[k] = v
	}

	mounts := make([]rt.MountConfig, 0, len(req.GetMounts()))
	for _, m := range req.GetMounts() {
		mounts = append(mounts, rt.MountConfig{
			Type:    m.GetType(),
			Source:  m.GetSource(),
			Target:  m.GetTarget(),
			Options: m.GetOptions(),
		})
	}

	rootfs := rt.RootfsConfig{}
	if funcRt.GetRootfs() != nil {
		protoRootfs := funcRt.GetRootfs()
		rootfs.Readonly = protoRootfs.GetReadonly()
		rootfs.Type = rt.RootfsSrcType(protoRootfs.GetType())
		rootfs.ImageURL = protoRootfs.GetImageUrl()
		if protoRootfs.GetS3Config() != nil {
			rootfs.S3 = &rt.S3Config{
				Endpoint:        protoRootfs.GetS3Config().GetEndpoint(),
				Bucket:          protoRootfs.GetS3Config().GetBucket(),
				Object:          protoRootfs.GetS3Config().GetObject(),
				AccessKeyID:     protoRootfs.GetS3Config().GetAccessKeyID(),
				AccessKeySecret: protoRootfs.GetS3Config().GetAccessKeySecret(),
			}
		}
	}

	cpuMilli := 500.0
	memMB := 512.0
	if v, ok := req.GetResources()["CPU"]; ok {
		cpuMilli = v
	}
	if v, ok := req.GetResources()["Memory"]; ok {
		memMB = v
	}

	network := req.GetNetwork()
	if network == "" {
		network = "host"
	}

	return &rt.CreateConfig{
		ID:           funcRt.GetId(),
		Sandbox:      funcRt.GetSandbox(),
		Rootfs:       rootfs,
		Command:      funcRt.GetCommand(),
		Envs:         envs,
		Mounts:       mounts,
		CPUMillicore: cpuMilli,
		MemoryMB:     memMB,
		Stdout:       req.GetStdout(),
		Stderr:       req.GetStderr(),
		ExtraConfig:  req.GetExtraConfig(),
		MakeSeed:     funcRt.GetMakeSeed(),
		Network:      network,
	}
}

// watchContainer 后台监听容器退出，更新状态。
func (s *LauncherService) watchContainer(containerID string) {
	status, err := s.runtime.Wait(context.Background(), containerID)
	if err != nil {
		log.Printf("[service] 监听容器 %s 退出失败: %v", containerID, err)
		s.stateMgr.MarkExited(containerID, 1, err.Error())
		return
	}
	s.stateMgr.MarkExited(containerID, status.ExitCode, status.Message)
}
