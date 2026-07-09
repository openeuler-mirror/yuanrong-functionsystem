package runtime

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"strconv"
	"strings"
	"time"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/filters"
	"github.com/docker/docker/api/types/image"
	"github.com/docker/docker/api/types/mount"
	"github.com/docker/docker/client"
	"github.com/docker/go-connections/nat"
)

const (
	shortContainerIDLength = 12
	runtimeLabelSlots      = 2
	containerNameModulo    = 100000
)

// DockerRuntime 使用 Docker Engine SDK 实现 ContainerRuntime 接口。
type DockerRuntime struct {
	client *client.Client
}

// NewDockerRuntime creates a Docker runtime backend.
// cfg supports the "docker_host" parameter for selecting the Docker daemon address.
func NewDockerRuntime(cfg map[string]string) (*DockerRuntime, error) {
	opts := []client.Opt{client.FromEnv, client.WithAPIVersionNegotiation()}
	if host, ok := cfg["docker_host"]; ok && host != "" {
		opts = append(opts, client.WithHost(host))
	}
	cli, err := client.NewClientWithOpts(opts...)
	if err != nil {
		return nil, fmt.Errorf("failed to create Docker client: %w", err)
	}
	return &DockerRuntime{client: cli}, nil
}

func (d *DockerRuntime) Name() string {
	return "docker"
}

func (d *DockerRuntime) Create(ctx context.Context, cfg *CreateConfig) (string, error) {
	// Resolve image name.
	imageName := cfg.Sandbox
	if cfg.Rootfs.Type == RootfsSrcImage && cfg.Rootfs.ImageURL != "" {
		imageName = cfg.Rootfs.ImageURL
	}
	if imageName == "" {
		return "", fmt.Errorf("container image is not specified: sandbox and imageUrl are both empty")
	}

	// Check whether the local image exists.
	_, _, err := d.client.ImageInspectWithRaw(ctx, imageName)
	if err != nil {
		// Local image is missing; try pulling it.
		log.Printf("[docker] local image %s is missing; pulling", imageName)
		pullOut, pullErr := d.client.ImagePull(ctx, imageName, image.PullOptions{})
		if pullErr != nil {
			return "", fmt.Errorf("failed to pull image %s: %w", imageName, pullErr)
		}
		// The pull output stream must be fully drained to ensure the pull is complete.
		_, _ = io.Copy(io.Discard, pullOut)
		pullOut.Close()
		log.Printf("[docker] image %s pull completed", imageName)
	} else {
		log.Printf("[docker] using local image %s", imageName)
	}

	// Build command by joining command tokens into a shell command.
	var shellCmd string
	if len(cfg.Command) > 0 {
		shellCmd = strings.Join(cfg.Command, " ")
	}
	var cmd []string
	if shellCmd != "" {
		cmd = []string{"/bin/sh", "-c", shellCmd}
	}

	// Build environment variables.
	envList := make([]string, 0, len(cfg.Envs))
	for k, v := range cfg.Envs {
		envList = append(envList, k+"="+v)
	}

	labels := make(map[string]string, len(cfg.Labels)+runtimeLabelSlots)
	for k, v := range cfg.Labels {
		labels[k] = v
	}
	labels[ManagedLabelKey] = ManagedLabelValue
	if cfg.ID != "" {
		labels[RuntimeIDLabelKey] = cfg.ID
	}

	// Container config.
	containerCfg := &container.Config{
		Image:  imageName,
		Cmd:    cmd,
		Env:    envList,
		Labels: labels,
	}

	// Host config: resource limits, mounts, and network.
	networkMode := container.NetworkMode(cfg.Network)
	if networkMode == "" {
		networkMode = "host" // Default to host networking.
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
			log.Printf("[docker] ignoring port mappings when network=host: %v", cfg.Ports)
		} else {
			containerCfg.ExposedPorts = exposedPorts
			hostCfg.PortBindings = portBindings
		}
	}

	// Create container.
	containerName := fmt.Sprintf("rl-%s-%d", cfg.ID, time.Now().UnixNano()%containerNameModulo)
	resp, err := d.client.ContainerCreate(ctx, containerCfg, hostCfg, nil, nil, containerName)
	if err != nil {
		return "", fmt.Errorf("failed to create container: %w", err)
	}

	// Start container.
	if err := d.client.ContainerStart(ctx, resp.ID, container.StartOptions{}); err != nil {
		// Startup failed; clean up the created container.
		_ = d.client.ContainerRemove(ctx, resp.ID, container.RemoveOptions{Force: true})
		return "", fmt.Errorf("failed to start container: %w", err)
	}

	log.Printf("[docker] container started: id=%s, image=%s, network=%s", resp.ID[:12], imageName, networkMode)
	return resp.ID, nil
}

func parsePortMappings(mappings []string) (nat.PortSet, nat.PortMap, error) {
	exposedPorts := nat.PortSet{}
	portBindings := nat.PortMap{}

	for _, raw := range mappings {
		parts := strings.Split(raw, ":")
		if len(parts) != 3 {
			return nil, nil, fmt.Errorf("invalid port mapping %q; expected format protocol:hostPort:containerPort", raw)
		}

		protocol := strings.ToLower(strings.TrimSpace(parts[0]))
		hostPort := strings.TrimSpace(parts[1])
		containerPort := strings.TrimSpace(parts[2])

		// L7 schemes (http/https/ws) declare how the sandbox router speaks to
		// the backend; the docker port binding underneath is always TCP.
		switch protocol {
		case "http", "https", "ws", "wss":
			protocol = "tcp"
		case "tcp", "udp", "sctp":
			// transport protocol passed through as-is
		default:
			return nil, nil, fmt.Errorf("invalid port mapping %q; protocol only supports http/https/tcp/udp/sctp", raw)
		}

		if err := validatePort(hostPort); err != nil {
			return nil, nil, fmt.Errorf("invalid port mapping %q; hostPort %v", raw, err)
		}
		if err := validatePort(containerPort); err != nil {
			return nil, nil, fmt.Errorf("invalid port mapping %q; containerPort %v", raw, err)
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
		return fmt.Errorf("not a valid integer port: %s", port)
	}
	if p < 1 || p > 65535 {
		return fmt.Errorf("out of range (1-65535): %d", p)
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
		return nil, fmt.Errorf("failed to wait for container exit: %w", err)
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func (d *DockerRuntime) Delete(ctx context.Context, containerID string, timeoutSeconds int64) error {
	// Always stop with timeout=0 (immediate SIGKILL).
	// A graceful SIGTERM wait is pointless here: the caller has already performed
	// application-level shutdown (ShutDownInstance) before invoking Delete.
	// Waiting for SIGTERM only delays the kill by up to gracefulshutdowntime seconds
	// and causes the container's Wait RPC to return before OnDeleteDone can unregister
	// the runtimeID, triggering a spurious NotifySandboxExit from OnWaitDone.
	zeroTimeout := 0
	stopOpts := container.StopOptions{Timeout: &zeroTimeout}
	if err := d.client.ContainerStop(ctx, containerID, stopOpts); err != nil {
		log.Printf("[docker] 停止容器 %s 失败: %v，尝试强制删除", containerID[:shortContainerIDLength], err)
	}

	// Force-delete container.
	err := d.client.ContainerRemove(ctx, containerID, container.RemoveOptions{Force: true})
	if err != nil {
		return fmt.Errorf("删除容器 %s 失败: %w", containerID[:shortContainerIDLength], err)
	}
	log.Printf("[docker] 容器已删除: %s", containerID[:shortContainerIDLength])
	return nil
}

// Close releases the Docker client resources.
func (d *DockerRuntime) Close() error {
	return d.client.Close()
}

// List returns runtime-launcher managed containers from Docker.
func (d *DockerRuntime) List(ctx context.Context, id string) ([]*ContainerInfo, error) {
	args := filters.NewArgs(filters.Arg("label", ManagedLabelKey+"="+ManagedLabelValue))
	containers, err := d.client.ContainerList(ctx, container.ListOptions{All: true, Filters: args})
	if err != nil {
		return nil, fmt.Errorf("failed to list containers: %w", err)
	}
	infos := make([]*ContainerInfo, 0, len(containers))
	for _, c := range containers {
		if id != "" && c.ID != id && !strings.HasPrefix(c.ID, id) {
			continue
		}
		labels := make(map[string]string, len(c.Labels))
		for k, v := range c.Labels {
			labels[k] = v
		}
		state := strings.ToLower(c.State)
		exitCode := int32(0)
		finishedAt := int64(0)
		if state == "exited" || state == "dead" {
			finishedAt = c.Created
		}
		infos = append(infos, &ContainerInfo{
			ID:         c.ID,
			RuntimeID:  labels[RuntimeIDLabelKey],
			Image:      c.Image,
			Command:    splitSummaryCommand(c.Command),
			Labels:     labels,
			State:      state,
			StartedAt:  c.Created,
			FinishedAt: finishedAt,
			ExitCode:   exitCode,
			Message:    c.Status,
		})
	}
	return infos, nil
}

func splitSummaryCommand(command string) []string {
	if command == "" {
		return nil
	}
	return []string{command}
}

// Stats reads real CPU/memory statistics through the Docker API.
// Use one-shot (non-streaming) mode to read a single stats snapshot.
func (d *DockerRuntime) Stats(ctx context.Context, containerID string) (*ContainerStats, error) {
	resp, err := d.client.ContainerStats(ctx, containerID, false)
	if err != nil {
		return nil, fmt.Errorf("failed to get container stats: %w", err)
	}
	defer resp.Body.Close()

	var raw container.StatsResponse
	if err := json.NewDecoder(resp.Body).Decode(&raw); err != nil {
		return nil, fmt.Errorf("failed to parse stats response: %w", err)
	}

	cs := &ContainerStats{
		CPUUsageNs:       raw.CPUStats.CPUUsage.TotalUsage,
		MemoryUsageBytes: raw.MemoryStats.Usage,
		MemoryLimitBytes: raw.MemoryStats.Limit,
		// MaxUsage is always 0 on cgroup v2; pass it through because callers already tolerate it.
		MemoryMaxUsageBytes: raw.MemoryStats.MaxUsage,
	}
	return cs, nil
}

// convertMounts converts MountConfig entries to Docker mount.Mount entries.
func convertMounts(mounts []MountConfig) []mount.Mount {
	result := make([]mount.Mount, 0, len(mounts))
	for _, m := range mounts {
		// Resolve mount source: prefer HostPath; S3/Image sources are not supported yet.
		source := m.HostPath
		if source == "" && m.ImageURL != "" {
			log.Printf("[docker] mount image_url source is not supported yet: target=%s, image=%s", m.Target, m.ImageURL)
		}
		if source == "" && m.S3 != nil {
			log.Printf("[docker] mount s3 source is not supported yet: target=%s, bucket=%s", m.Target, m.S3.Bucket)
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
			// Treat erofs as a read-only bind mount.
			dm.Type = mount.TypeBind
			dm.ReadOnly = true
		default:
			dm.Type = mount.TypeBind
		}
		result = append(result, dm)
	}
	return result
}
