package main

import (
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"

	rt "runtime-launcher/internal/runtime"
	"runtime-launcher/internal/server"
	"runtime-launcher/internal/service"
	"runtime-launcher/internal/state"
)

func main() {
	// 命令行参数
	socketPath := flag.String("socket", "/var/run/runtime-launcher.sock", "UDS socket 路径")
	backend := flag.String("backend", "docker", "容器运行时后端: docker|podman")
	dockerHost := flag.String("docker-host", "", "Docker daemon 地址（仅 docker 后端）")
	podmanSocket := flag.String("podman-socket", "", "Podman socket 地址（仅 podman 后端）")
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lshortfile)
	log.Printf("runtime-launcher 启动中...")
	log.Printf("  后端: %s", *backend)
	log.Printf("  socket: %s", *socketPath)

	// 构建后端配置
	cfg := make(map[string]string)
	if *dockerHost != "" {
		cfg["docker_host"] = *dockerHost
	}
	if *podmanSocket != "" {
		cfg["podman_socket"] = *podmanSocket
	}

	// 初始化运行时后端
	containerRuntime, err := rt.NewRuntime(*backend, cfg)
	if err != nil {
		log.Fatalf("初始化运行时后端失败: %v", err)
	}
	defer containerRuntime.Close()

	log.Printf("  运行时后端 [%s] 已初始化", containerRuntime.Name())

	// 初始化状态管理器
	stateMgr := state.NewManager()

	// 创建 gRPC 服务
	svc := service.NewLauncherService(containerRuntime, stateMgr)

	// 创建 gRPC 服务器
	srv, err := server.New(*socketPath, svc)
	if err != nil {
		log.Fatalf("创建 gRPC 服务器失败: %v", err)
	}

	// 信号处理：SIGINT/SIGTERM -> 优雅关闭
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigCh
		log.Printf("收到信号 %v，正在关闭...", sig)
		srv.GracefulStop()
		containerRuntime.Close()
	}()

	log.Printf("runtime-launcher 已就绪，监听 unix://%s (后端: %s)", *socketPath, *backend)

	if err := srv.Run(); err != nil {
		log.Fatalf("服务器运行失败: %v", err)
	}
}
