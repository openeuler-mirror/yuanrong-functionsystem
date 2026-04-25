package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	pb "runtime-launcher/api/proto/runtime/v1"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

func main() {
	// 命令行参数
	socketPath := flag.String("socket", "/var/run/runtime-launcher.sock", "RuntimeLauncher UDS 路径")
	action := flag.String("action", "run", "操作: run | start | wait | delete | register | unregister | list")
	imageFlag := flag.String("image", "", "容器镜像（必填，用于 run/start）")
	cmdFlag := flag.String("cmd", "", "容器内执行的命令（如 'echo hello'）")
	mountFlag := flag.String("mount", "", "挂载，格式: 源路径:目标路径[:ro]，多个用逗号分隔")
	networkFlag := flag.String("network", "bridge", "网络模式（如 bridge|host|none）")
	portsFlag := flag.String("ports", "", "端口映射，格式: protocol:hostPort:containerPort，多个用逗号分隔")
	idFlag := flag.String("id", "", "容器/运行时 ID（用于 wait/delete/unregister）")
	timeoutFlag := flag.Int64("timeout", 5, "删除时的优雅超时秒数")
	cpuFlag := flag.Float64("cpu", 500, "CPU 毫核")
	memFlag := flag.Float64("mem", 512, "内存 MB")
	envFlag := flag.String("env", "", "环境变量，格式: KEY=VAL，多个用逗号分隔")
	flag.Parse()

	log.SetFlags(log.LstdFlags | log.Lshortfile)

	// 连接 gRPC 服务
	conn, err := grpc.NewClient(
		"unix://"+*socketPath,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		log.Fatalf("连接失败: %v", err)
	}
	defer conn.Close()

	client := pb.NewRuntimeLauncherClient(conn)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()

	switch *action {
	case "run":
		// run = start + wait + delete（完整生命周期）
		doRun(ctx, client, *imageFlag, *cmdFlag, *mountFlag, *envFlag, *networkFlag, *portsFlag, *cpuFlag, *memFlag, *timeoutFlag)
	case "start":
		doStart(ctx, client, *imageFlag, *cmdFlag, *mountFlag, *envFlag, *networkFlag, *portsFlag, *cpuFlag, *memFlag)
	case "wait":
		doWait(ctx, client, *idFlag)
	case "delete":
		doDelete(ctx, client, *idFlag, *timeoutFlag)
	case "register":
		doRegister(ctx, client, *imageFlag, *idFlag, *cmdFlag, *envFlag)
	case "unregister":
		doUnregister(ctx, client, *idFlag)
	case "list":
		doList(ctx, client)
	default:
		fmt.Fprintf(os.Stderr, "未知操作: %s\n可选: run, start, wait, delete, register, unregister, list\n", *action)
		os.Exit(1)
	}
}

// doRun 执行完整的容器生命周期：start -> wait -> delete
func doRun(ctx context.Context, client pb.RuntimeLauncherClient, image, cmd, mounts, envs, network, ports string, cpu, mem float64, timeout int64) {
	if image == "" {
		log.Fatal("--image 参数必填")
	}

	// 1. Start
	containerID := mustStart(ctx, client, image, cmd, mounts, envs, network, ports, cpu, mem)

	// 2. Wait
	fmt.Println("\n--- 等待容器退出 ---")
	waitResp, err := client.Wait(ctx, &pb.WaitRequest{Id: containerID})
	if err != nil {
		log.Printf("Wait RPC 失败: %v", err)
	} else {
		fmt.Printf("容器已退出:\n")
		fmt.Printf("  exit_code: %d\n", waitResp.GetExitCode())
		fmt.Printf("  status:    %d\n", waitResp.GetStatus())
		if waitResp.GetMessage() != "" {
			fmt.Printf("  message:   %s\n", waitResp.GetMessage())
		}
	}

	// 3. Delete
	fmt.Println("\n--- 删除容器 ---")
	_, err = client.Delete(ctx, &pb.DeleteRequest{Id: containerID, Timeout: timeout})
	if err != nil {
		log.Printf("Delete 失败: %v", err)
	} else {
		fmt.Println("容器已删除")
	}
}

func doStart(ctx context.Context, client pb.RuntimeLauncherClient, image, cmd, mounts, envs, network, ports string, cpu, mem float64) {
	if image == "" {
		log.Fatal("--image 参数必填")
	}
	mustStart(ctx, client, image, cmd, mounts, envs, network, ports, cpu, mem)
}

func mustStart(ctx context.Context, client pb.RuntimeLauncherClient, image, cmd, mounts, envs, network, ports string, cpu, mem float64) string {
	runtimeID := fmt.Sprintf("test-%d", time.Now().UnixNano()%1000000)

	// 构建命令
	var command []string
	if cmd != "" {
		command = strings.Fields(cmd)
	}

	// 构建挂载
	var mountList []*pb.Mount
	if mounts != "" {
		for _, m := range strings.Split(mounts, ",") {
			parts := strings.SplitN(m, ":", 3)
			if len(parts) < 2 {
				log.Fatalf("挂载格式错误: %s（应为 源:目标[:ro]）", m)
			}
			mt := &pb.Mount{
				Type:   "bind",
				Target: parts[1],
				Source: &pb.Mount_HostPath{HostPath: parts[0]},
			}
			if len(parts) == 3 && parts[2] == "ro" {
				mt.Options = []string{"ro"}
			}
			mountList = append(mountList, mt)
		}
	}

	// 构建环境变量
	userEnvs := make(map[string]string)
	if envs != "" {
		for _, e := range strings.Split(envs, ",") {
			kv := strings.SplitN(e, "=", 2)
			if len(kv) == 2 {
				userEnvs[kv[0]] = kv[1]
			}
		}
	}

	// 构建端口映射
	var portMappings []string
	if ports != "" {
		for _, p := range strings.Split(ports, ",") {
			port := strings.TrimSpace(p)
			if port != "" {
				portMappings = append(portMappings, port)
			}
		}
	}

	req := &pb.StartRequest{
		FuncRuntime: &pb.FunctionRuntime{
			Id:      runtimeID,
			Sandbox: image,
			Rootfs: &pb.RootfsConfig{
				Type: pb.RootfsSrcType_IMAGE,
			},
			Command: command,
		},
		Mounts: mountList,
		Resources: map[string]float64{
			"CPU":    cpu,
			"Memory": mem,
		},
		UserEnvs: userEnvs,
		Network:  network,
		Ports:    portMappings,
	}

	fmt.Println("--- 启动容器 ---")
	fmt.Printf("  镜像:    %s\n", image)
	fmt.Printf("  命令:    %s\n", cmd)
	fmt.Printf("  CPU:     %.0f millicore\n", cpu)
	fmt.Printf("  内存:    %.0f MB\n", mem)
	fmt.Printf("  网络:    %s\n", network)
	if len(mountList) > 0 {
		fmt.Printf("  挂载:\n")
		for _, m := range mountList {
			fmt.Printf("    %s -> %s\n", m.GetHostPath(), m.Target)
		}
	}
	if len(userEnvs) > 0 {
		fmt.Printf("  环境变量:\n")
		for k, v := range userEnvs {
			fmt.Printf("    %s=%s\n", k, v)
		}
	}
	if len(portMappings) > 0 {
		fmt.Printf("  端口映射:\n")
		for _, p := range portMappings {
			fmt.Printf("    %s\n", p)
		}
	}
	fmt.Println()

	resp, err := client.Start(ctx, req)
	if err != nil {
		log.Fatalf("Start RPC 失败: %v", err)
	}
	if resp.GetCode() != 0 {
		log.Fatalf("Start 失败: code=%d, message=%s", resp.GetCode(), resp.GetMessage())
	}

	fmt.Printf("容器已启动: id=%s\n", resp.GetId())
	return resp.GetId()
}

func doWait(ctx context.Context, client pb.RuntimeLauncherClient, id string) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	fmt.Printf("等待容器 %s 退出...\n", id)
	resp, err := client.Wait(ctx, &pb.WaitRequest{Id: id})
	if err != nil {
		log.Fatalf("Wait 失败: %v", err)
	}
	fmt.Printf("容器已退出: exit_code=%d, status=%d, message=%s\n",
		resp.GetExitCode(), resp.GetStatus(), resp.GetMessage())
}

func doDelete(ctx context.Context, client pb.RuntimeLauncherClient, id string, timeout int64) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	fmt.Printf("删除容器 %s (timeout=%ds)...\n", id, timeout)
	_, err := client.Delete(ctx, &pb.DeleteRequest{Id: id, Timeout: timeout})
	if err != nil {
		log.Fatalf("Delete 失败: %v", err)
	}
	fmt.Println("容器已删除")
}

func doRegister(ctx context.Context, client pb.RuntimeLauncherClient, image, id, cmd, envs string) {
	if id == "" {
		id = fmt.Sprintf("reg-%d", time.Now().UnixNano()%1000000)
	}
	var command []string
	if cmd != "" {
		command = strings.Fields(cmd)
	}
	runtimeEnvs := make(map[string]string)
	if envs != "" {
		for _, e := range strings.Split(envs, ",") {
			kv := strings.SplitN(e, "=", 2)
			if len(kv) == 2 {
				runtimeEnvs[kv[0]] = kv[1]
			}
		}
	}

	req := &pb.RegisterRequest{
		FuncRuntimes: []*pb.FunctionRuntime{
			{
				Id:          id,
				Sandbox:     image,
				Command:     command,
				RuntimeEnvs: runtimeEnvs,
			},
		},
	}
	resp, err := client.Register(ctx, req)
	if err != nil {
		log.Fatalf("Register 失败: %v", err)
	}
	fmt.Printf("注册结果: success=%v, message=%s\n", resp.GetSuccess(), resp.GetMessage())
}

func doUnregister(ctx context.Context, client pb.RuntimeLauncherClient, id string) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	ids := strings.Split(id, ",")
	resp, err := client.Unregister(ctx, &pb.UnregisterRequest{Ids: ids})
	if err != nil {
		log.Fatalf("Unregister 失败: %v", err)
	}
	fmt.Printf("注销结果: success=%v, message=%s\n", resp.GetSuccess(), resp.GetMessage())
}

func doList(ctx context.Context, client pb.RuntimeLauncherClient) {
	resp, err := client.GetRegistered(ctx, &pb.GetRegisteredRequest{})
	if err != nil {
		log.Fatalf("GetRegistered 失败: %v", err)
	}
	runtimes := resp.GetFuncRuntimes()
	if len(runtimes) == 0 {
		fmt.Println("无已注册的运行时")
		return
	}
	fmt.Printf("已注册的运行时（共 %d 个）:\n", len(runtimes))
	for i, rt := range runtimes {
		fmt.Printf("  [%d] id=%s, sandbox=%s, makeSeed=%v, command=%v\n",
			i+1, rt.GetId(), rt.GetSandbox(), rt.GetMakeSeed(), rt.GetCommand())
	}
}
