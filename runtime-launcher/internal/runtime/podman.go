package runtime

import (
	"context"
	"fmt"
	"log"

	"github.com/docker/docker/client"
)

// PodmanRuntime 使用 Podman 的 Docker 兼容 API 实现 ContainerRuntime 接口。
// Podman 提供与 Docker 兼容的 REST API，因此复用 Docker SDK，仅连接地址不同。
type PodmanRuntime struct {
	inner *DockerRuntime // 内部委托给 DockerRuntime 实现
	name  string
}

const defaultPodmanSocket = "unix:///run/podman/podman.sock"

// NewPodmanRuntime 创建 Podman 运行时后端。
// cfg 支持 "podman_socket" 参数指定 Podman socket 地址。
func NewPodmanRuntime(cfg map[string]string) (*PodmanRuntime, error) {
	socket := defaultPodmanSocket
	if s, ok := cfg["podman_socket"]; ok && s != "" {
		socket = s
	}

	cli, err := client.NewClientWithOpts(
		client.WithHost(socket),
		client.WithAPIVersionNegotiation(),
	)
	if err != nil {
		return nil, fmt.Errorf("创建 Podman 客户端失败: %w", err)
	}

	log.Printf("[podman] 连接到 Podman socket: %s", socket)
	return &PodmanRuntime{
		inner: &DockerRuntime{client: cli},
		name:  "podman",
	}, nil
}

func (p *PodmanRuntime) Name() string {
	return p.name
}

func (p *PodmanRuntime) Create(ctx context.Context, cfg *CreateConfig) (string, error) {
	return p.inner.Create(ctx, cfg)
}

func (p *PodmanRuntime) Wait(ctx context.Context, containerID string) (*ContainerStatus, error) {
	return p.inner.Wait(ctx, containerID)
}

func (p *PodmanRuntime) Delete(ctx context.Context, containerID string, timeoutSeconds int64) error {
	return p.inner.Delete(ctx, containerID, timeoutSeconds)
}

func (p *PodmanRuntime) Close() error {
	return p.inner.Close()
}
