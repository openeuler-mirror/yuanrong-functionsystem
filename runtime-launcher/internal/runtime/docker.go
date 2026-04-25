package runtime

import (
	"context"
	"fmt"
	"io"
	"log"
	"strconv"
	"strings"
	"time"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/image"
	"github.com/docker/docker/api/types/mount"
	"github.com/docker/docker/client"
	"github.com/docker/go-connections/nat"
)

// DockerRuntime 使用 Docker Engine SDK 实现 ContainerRuntime 接口。
type DockerRuntime struct {
	client *client.Client
}

// NewDockerRuntime 创建 Docker 运行时后端。
// cfg 支持 "docker_host" 参数指定 Docker daemon 地址。
func NewDockerRuntime(cfg map[string]string) (*DockerRuntime, error) {
	opts := []client.Opt{client.FromEnv, client.WithAPIVersionNegotiation()}
	if host, ok := cfg["docker_host"]; ok && host != "" {
		opts = append(opts, client.WithHost(host))
	}
	cli, err := client.NewClientWithOpts(opts...)
	if err != nil {
		return nil, fmt.Errorf("创建 Docker 客户端失败: %w", err)
	}
	return &DockerRuntime{client: cli}, nil
}

func (d *DockerRuntime) Name() string {
	return "docker"
}

func (d *DockerRuntime) Create(ctx context.Context, cfg *CreateConfig) (string, error) {
	// 确定镜像名称
	imageName := cfg.Sandbox
	if cfg.Rootfs.Type == RootfsSrcImage && cfg.Rootfs.ImageURL != "" {
		imageName = cfg.Rootfs.ImageURL
	}
	if imageName == "" {
		return "", fmt.Errorf("未指定容器镜像（sandbox 和 imageUrl 均为空）")
	}

	// 检查本地镜像是否存在
	_, _, err := d.client.ImageInspectWithRaw(ctx, imageName)
	if err != nil {
		// 本地镜像不存在，尝试拉取
		log.Printf("[docker] 本地镜像 %s 不存在，开始拉取", imageName)
		pullOut, pullErr := d.client.ImagePull(ctx, imageName, image.PullOptions{})
		if pullErr != nil {
			return "", fmt.Errorf("拉取镜像 %s 失败: %w", imageName, pullErr)
		}
		// 必须读完 pull 输出流才能确保拉取完成
		_, _ = io.Copy(io.Discard, pullOut)
		pullOut.Close()
		log.Printf("[docker] 镜像 %s 拉取完成", imageName)
	} else {
		log.Printf("[docker] 使用本地镜像 %s", imageName)
	}

	// 构建命令：将 command tokens 拼接为 shell 命令
	var shellCmd string
	if len(cfg.Command) > 0 {
		shellCmd = strings.Join(cfg.Command, " ")
	}
	var cmd []string
	if shellCmd != "" {
		cmd = []string{"/bin/sh", "-c", shellCmd}
	}

	// 构建环境变量
	envList := make([]string, 0, len(cfg.Envs))
	for k, v := range cfg.Envs {
		envList = append(envList, k+"="+v)
	}

	// 容器配置
	containerCfg := &container.Config{
		Image: imageName,
		Cmd:   cmd,
		Env:   envList,
	}

	// 主机配置（资源限制 + 挂载 + 网络）
	networkMode := container.NetworkMode(cfg.Network)
	if networkMode == "" {
		networkMode = "host" // 默认使用 host 网络
	}

	hostCfg := &container.HostConfig{
		Resources: container.Resources{
			NanoCPUs: int64(cfg.CPUMillicore * 1e6), // millicore -> nanocpu
			Memory:   int64(cfg.MemoryMB * 1024 * 1024),
		},
		Mounts:      convertMounts(cfg.Mounts),
		NetworkMode: networkMode,
	}

	if len(cfg.Ports) > 0 {
		exposedPorts, portBindings, err := parsePortMappings(cfg.Ports)
		if err != nil {
			return "", err
		}

		if networkMode == "host" {
			log.Printf("[docker] network=host 时忽略端口映射: %v", cfg.Ports)
		} else {
			containerCfg.ExposedPorts = exposedPorts
			hostCfg.PortBindings = portBindings
		}
	}

	// 创建容器
	containerName := fmt.Sprintf("rl-%s-%d", cfg.ID, time.Now().UnixNano()%100000)
	resp, err := d.client.ContainerCreate(ctx, containerCfg, hostCfg, nil, nil, containerName)
	if err != nil {
		return "", fmt.Errorf("创建容器失败: %w", err)
	}

	// 启动容器
	if err := d.client.ContainerStart(ctx, resp.ID, container.StartOptions{}); err != nil {
		// 启动失败，清理已创建的容器
		_ = d.client.ContainerRemove(ctx, resp.ID, container.RemoveOptions{Force: true})
		return "", fmt.Errorf("启动容器失败: %w", err)
	}

	log.Printf("[docker] 容器已启动: id=%s, image=%s, network=%s", resp.ID[:12], imageName, networkMode)
	return resp.ID, nil
}

func parsePortMappings(mappings []string) (nat.PortSet, nat.PortMap, error) {
	exposedPorts := nat.PortSet{}
	portBindings := nat.PortMap{}

	for _, raw := range mappings {
		parts := strings.Split(raw, ":")
		if len(parts) != 3 {
			return nil, nil, fmt.Errorf("无效端口映射 %q，期望格式 protocol:hostPort:containerPort", raw)
		}

		protocol := strings.ToLower(strings.TrimSpace(parts[0]))
		hostPort := strings.TrimSpace(parts[1])
		containerPort := strings.TrimSpace(parts[2])

		if protocol != "tcp" && protocol != "udp" && protocol != "sctp" {
			return nil, nil, fmt.Errorf("无效端口映射 %q，protocol 仅支持 tcp/udp/sctp", raw)
		}

		if err := validatePort(hostPort); err != nil {
			return nil, nil, fmt.Errorf("无效端口映射 %q，hostPort %v", raw, err)
		}
		if err := validatePort(containerPort); err != nil {
			return nil, nil, fmt.Errorf("无效端口映射 %q，containerPort %v", raw, err)
		}

		portKey := nat.Port(fmt.Sprintf("%s/%s", containerPort, protocol))
		exposedPorts[portKey] = struct{}{}
		portBindings[portKey] = append(portBindings[portKey], nat.PortBinding{HostPort: hostPort})
	}

	return exposedPorts, portBindings, nil
}

func validatePort(port string) error {
	p, err := strconv.Atoi(port)
	if err != nil {
		return fmt.Errorf("不是合法整数端口: %s", port)
	}
	if p < 1 || p > 65535 {
		return fmt.Errorf("超出范围(1-65535): %d", p)
	}
	return nil
}

func (d *DockerRuntime) Wait(ctx context.Context, containerID string) (*ContainerStatus, error) {
	resultCh, errCh := d.client.ContainerWait(ctx, containerID, container.WaitConditionNotRunning)
	select {
	case result := <-resultCh:
		msg := ""
		if result.Error != nil {
			msg = result.Error.Message
		}
		return &ContainerStatus{
			StatusCode: int32(result.StatusCode),
			ExitCode:   int32(result.StatusCode),
			Message:    msg,
		}, nil
	case err := <-errCh:
		return nil, fmt.Errorf("等待容器退出失败: %w", err)
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func (d *DockerRuntime) Delete(ctx context.Context, containerID string, timeoutSeconds int64) error {
	if timeoutSeconds > 0 {
		// 优雅停止：先发 SIGTERM，等待超时后 SIGKILL
		timeout := int(timeoutSeconds)
		stopOpts := container.StopOptions{Timeout: &timeout}
		if err := d.client.ContainerStop(ctx, containerID, stopOpts); err != nil {
			log.Printf("[docker] 优雅停止容器 %s 失败: %v，尝试强制删除", containerID[:12], err)
		}
	}

	// 强制删除容器
	err := d.client.ContainerRemove(ctx, containerID, container.RemoveOptions{Force: true})
	if err != nil {
		return fmt.Errorf("删除容器 %s 失败: %w", containerID[:12], err)
	}
	log.Printf("[docker] 容器已删除: %s", containerID[:12])
	return nil
}

func (d *DockerRuntime) Close() error {
	return d.client.Close()
}

// convertMounts 将 MountConfig 列表转为 Docker mount.Mount 列表。
func convertMounts(mounts []MountConfig) []mount.Mount {
	result := make([]mount.Mount, 0, len(mounts))
	for _, m := range mounts {
		// 确定挂载源：优先 HostPath，S3/Image 暂不支持
		source := m.HostPath
		if source == "" && m.ImageURL != "" {
			log.Printf("[docker] mount image_url 源暂不支持: target=%s, image=%s", m.Target, m.ImageURL)
		}
		if source == "" && m.S3 != nil {
			log.Printf("[docker] mount s3 源暂不支持: target=%s, bucket=%s", m.Target, m.S3.Bucket)
		}

		dm := mount.Mount{
			Source: source,
			Target: m.Target,
		}
		switch strings.ToLower(m.Type) {
		case "bind":
			dm.Type = mount.TypeBind
		case "volume":
			dm.Type = mount.TypeVolume
		case "tmpfs":
			dm.Type = mount.TypeTmpfs
		case "erofs":
			// erofs 作为只读 bind mount 处理
			dm.Type = mount.TypeBind
			dm.ReadOnly = true
		default:
			dm.Type = mount.TypeBind
		}
		result = append(result, dm)
	}
	return result
}
