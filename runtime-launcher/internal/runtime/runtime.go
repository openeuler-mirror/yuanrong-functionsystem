package runtime

import (
	"context"
	"fmt"
)

// ContainerRuntime 是容器运行时后端的统一接口。
// docker 和 podman 各自实现此接口。
type ContainerRuntime interface {
	// Name 返回运行时后端名称（如 "docker"、"podman"）。
	Name() string

	// Create 创建并启动一个容器，返回容器 ID。
	// 此方法应启动容器进程后立即返回，不阻塞等待容器退出。
	Create(ctx context.Context, cfg *CreateConfig) (containerID string, err error)

	// Wait 阻塞直到容器退出，返回退出状态。
	Wait(ctx context.Context, containerID string) (*ContainerStatus, error)

	// Delete 停止并删除容器。timeout=0 表示强制杀死。
	Delete(ctx context.Context, containerID string, timeoutSeconds int64) error

	// Close 在服务关闭时执行清理。
	Close() error
}

// CreateConfig 封装了创建和启动容器所需的全部配置。
type CreateConfig struct {
	// 标识
	ID      string // FunctionRuntime.id
	Sandbox string // 容器镜像/沙箱名称

	// Rootfs 配置
	Rootfs RootfsConfig

	// 执行
	Command []string          // FunctionRuntime.command
	Envs    map[string]string // 合并后的 runtimeEnvs + userEnvs

	// 挂载
	Mounts []MountConfig

	// 资源限制
	CPUMillicore float64 // CPU 毫核
	MemoryMB     float64 // 内存 MB

	// IO 重定向
	Stdout string // stdout 重定向路径
	Stderr string // stderr 重定向路径

	// 扩展配置
	ExtraConfig string // 后端特定的 JSON 配置
	MakeSeed    bool   // 是否为预热种子容器

	// 网络配置
	Network string // 网络模式：host, bridge, none 等，默认 host
}

// MountConfig 对应 proto Mount 消息。
type MountConfig struct {
	Type    string
	Source  string
	Target  string
	Options []string
}

// RootfsConfig 对应 proto RootfsConfig 消息。
type RootfsConfig struct {
	Readonly bool
	Type     RootfsSrcType
	ImageURL string
	S3       *S3Config
}

// RootfsSrcType rootfs 来源类型。
type RootfsSrcType int

const (
	RootfsSrcS3    RootfsSrcType = 0
	RootfsSrcImage RootfsSrcType = 1
)

// S3Config S3 存储配置。
type S3Config struct {
	Endpoint        string
	Bucket          string
	Object          string
	AccessKeyID     string
	AccessKeySecret string
}

// ContainerStatus 容器退出状态。
type ContainerStatus struct {
	StatusCode int32
	ExitCode   int32
	Message    string
}

// NewRuntime 根据后端名称创建对应的 ContainerRuntime 实例。
func NewRuntime(backend string, cfg map[string]string) (ContainerRuntime, error) {
	switch backend {
	case "docker":
		return NewDockerRuntime(cfg)
	case "podman":
		return NewPodmanRuntime(cfg)
	default:
		return nil, fmt.Errorf("不支持的运行时后端: %s (可选: docker, podman)", backend)
	}
}
