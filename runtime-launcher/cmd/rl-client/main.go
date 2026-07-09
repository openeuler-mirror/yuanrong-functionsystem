package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	"runtime-launcher/api/proto/runtime/v1"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

type startOptions struct {
	image       string
	commandLine string
	mounts      string
	envs        string
	network     string
	ports       string
	cpu         float64
	memory      float64
}

type registerOptions struct {
	image       string
	id          string
	commandLine string
	envs        string
}

type cliFlags struct {
	socketPath *string
	action     *string
	image      *string
	command    *string
	mounts     *string
	network    *string
	ports      *string
	id         *string
	timeout    *int64
	cpu        *float64
	memory     *float64
	envs       *string
}

func main() {
	flags := parseFlags()
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	conn, err := connect(*flags.socketPath)
	if err != nil {
		log.Fatalf("连接失败: %v", err)
	}
	defer conn.Close()

	client := runtimev1.NewSandboxServiceClient(conn)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()
	runAction(ctx, client, flags)
}

func parseFlags() cliFlags {
	flags := cliFlags{
		socketPath: flag.String("socket", "/var/run/runtime-launcher.sock", "SandboxService UDS 路径"),
		action: flag.String(
			"action",
			"run",
			"操作: run | start | wait | delete | register | unregister | list | list-sandboxes | list-registered",
		),
		image:   flag.String("image", "", "容器镜像（必填，用于 run/start）"),
		command: flag.String("cmd", "", "容器内执行的命令（如 'echo hello'）"),
		mounts:  flag.String("mount", "", "挂载，格式: 源路径:目标路径[:ro]，多个用逗号分隔"),
		network: flag.String("network", "bridge", "网络模式（如 bridge|host|none）"),
		ports:   flag.String("ports", "", "端口映射，格式: protocol:hostPort:containerPort，多个用逗号分隔"),
		id:      flag.String("id", "", "容器/运行时 ID（用于 wait/delete/unregister）"),
		timeout: flag.Int64("timeout", 5, "删除时的优雅超时秒数"),
		cpu:     flag.Float64("cpu", 500, "CPU 毫核"),
		memory:  flag.Float64("mem", 512, "内存 MB"),
		envs:    flag.String("env", "", "环境变量，格式: KEY=VAL，多个用逗号分隔"),
	}
	flag.Parse()
	return flags
}

func connect(socketPath string) (*grpc.ClientConn, error) {
	return grpc.NewClient(
		"unix://"+socketPath,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
}

func runAction(ctx context.Context, client runtimev1.SandboxServiceClient, flags cliFlags) {
	switch *flags.action {
	case "run":
		doRun(ctx, client, startOptionsFromFlags(flags), *flags.timeout)
	case "start":
		doStart(ctx, client, startOptionsFromFlags(flags))
	case "wait":
		doWait(ctx, client, *flags.id)
	case "delete":
		doDelete(ctx, client, *flags.id, *flags.timeout)
	case "register":
		doRegister(ctx, client, registerOptionsFromFlags(flags))
	case "unregister":
		doUnregister(ctx, client, *flags.id)
	case "list", "list-sandboxes":
		doListSandboxes(ctx, client)
	case "list-registered":
		doListRegistered(ctx, client)
	default:
		fmt.Fprintf(
			os.Stderr,
			"未知操作: %s\n可选: run, start, wait, delete, register, unregister, list, list-sandboxes, list-registered\n",
			*flags.action,
		)
		os.Exit(1)
	}
}

func startOptionsFromFlags(flags cliFlags) startOptions {
	return startOptions{
		image:       *flags.image,
		commandLine: *flags.command,
		mounts:      *flags.mounts,
		envs:        *flags.envs,
		network:     *flags.network,
		ports:       *flags.ports,
		cpu:         *flags.cpu,
		memory:      *flags.memory,
	}
}

func registerOptionsFromFlags(flags cliFlags) registerOptions {
	return registerOptions{
		image:       *flags.image,
		id:          *flags.id,
		commandLine: *flags.command,
		envs:        *flags.envs,
	}
}

// doRun 执行完整的容器生命周期：start -> wait -> delete
func doRun(ctx context.Context, client runtimev1.SandboxServiceClient, opts startOptions, timeout int64) {
	if opts.image == "" {
		log.Fatal("--image 参数必填")
	}

	// 1. Start
	containerID := mustStart(ctx, client, opts)

	// 2. Wait
	fmt.Println("\n--- 等待容器退出 ---")
	waitResp, err := client.Wait(ctx, &runtimev1.WaitRequest{Id: containerID})
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
	_, err = client.Delete(ctx, &runtimev1.DeleteRequest{Id: containerID, Timeout: timeout})
	if err != nil {
		log.Printf("Delete 失败: %v", err)
	} else {
		fmt.Println("容器已删除")
	}
}

func doStart(ctx context.Context, client runtimev1.SandboxServiceClient, opts startOptions) {
	if opts.image == "" {
		log.Fatal("--image 参数必填")
	}
	mustStart(ctx, client, opts)
}

func mustStart(ctx context.Context, client runtimev1.SandboxServiceClient, opts startOptions) string {
	runtimeID := fmt.Sprintf("test-%d", time.Now().UnixNano()%1000000)
	req := buildStartRequest(runtimeID, opts)
	printStartRequest(opts, req)
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

func buildStartRequest(runtimeID string, opts startOptions) *runtimev1.SandboxStartRequest {
	command := parseCommand(opts.commandLine)
	mountList := parseMounts(opts.mounts)
	userEnvs := parseEnvMap(opts.envs)
	portMappings := parsePorts(opts.ports)
	return &runtimev1.SandboxStartRequest{
		SandboxId: runtimeID,
		Runtime:   opts.image,
		Rootfs: &runtimev1.RootfsConfig{
			Type:   runtimev1.RootfsSrcType_IMAGE,
			Source: &runtimev1.RootfsConfig_ImageUrl{ImageUrl: opts.image},
		},
		Command:   command,
		Mounts:    mountList,
		Resources: map[string]float64{"CPU": opts.cpu, "Memory": opts.memory},
		Envs:      userEnvs,
		Network:   opts.network,
		Ports:     portMappings,
	}
}

func parseCommand(commandLine string) []string {
	if commandLine == "" {
		return nil
	}
	return strings.Fields(commandLine)
}

func parseMounts(mounts string) []*runtimev1.Mount {
	var mountList []*runtimev1.Mount
	if mounts == "" {
		return mountList
	}
	for _, m := range strings.Split(mounts, ",") {
		parts := strings.SplitN(m, ":", 3)
		if len(parts) < 2 {
			log.Fatalf("挂载格式错误: %s（应为 源:目标[:ro]）", m)
		}
		mt := &runtimev1.Mount{
			Type:   "bind",
			Target: parts[1],
			Source: &runtimev1.Mount_HostPath{HostPath: parts[0]},
		}
		if len(parts) == 3 && parts[2] == "ro" {
			mt.Options = []string{"ro"}
		}
		mountList = append(mountList, mt)
	}
	return mountList
}

func parseEnvMap(envs string) map[string]string {
	userEnvs := make(map[string]string)
	if envs == "" {
		return userEnvs
	}
	for _, e := range strings.Split(envs, ",") {
		kv := strings.SplitN(e, "=", 2)
		if len(kv) == 2 {
			userEnvs[kv[0]] = kv[1]
		}
	}
	return userEnvs
}

func parsePorts(ports string) []string {
	var portMappings []string
	if ports == "" {
		return portMappings
	}
	for _, p := range strings.Split(ports, ",") {
		port := strings.TrimSpace(p)
		if port != "" {
			portMappings = append(portMappings, port)
		}
	}
	return portMappings
}

func printStartRequest(opts startOptions, req *runtimev1.SandboxStartRequest) {
	fmt.Println("--- 启动容器 ---")
	fmt.Printf("  镜像:    %s\n", opts.image)
	fmt.Printf("  命令:    %s\n", opts.commandLine)
	fmt.Printf("  CPU:     %.0f millicore\n", opts.cpu)
	fmt.Printf("  内存:    %.0f MB\n", opts.memory)
	fmt.Printf("  网络:    %s\n", opts.network)
	if len(req.GetMounts()) > 0 {
		fmt.Printf("  挂载:\n")
		for _, m := range req.GetMounts() {
			fmt.Printf("    %s -> %s\n", m.GetHostPath(), m.Target)
		}
	}
	if len(req.GetEnvs()) > 0 {
		fmt.Printf("  环境变量:\n")
		for k, v := range req.GetEnvs() {
			fmt.Printf("    %s=%s\n", k, v)
		}
	}
	if len(req.GetPorts()) > 0 {
		fmt.Printf("  端口映射:\n")
		for _, p := range req.GetPorts() {
			fmt.Printf("    %s\n", p)
		}
	}
	fmt.Println()
}

func doWait(ctx context.Context, client runtimev1.SandboxServiceClient, id string) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	fmt.Printf("等待容器 %s 退出...\n", id)
	resp, err := client.Wait(ctx, &runtimev1.WaitRequest{Id: id})
	if err != nil {
		log.Fatalf("Wait 失败: %v", err)
	}
	fmt.Printf("容器已退出: exit_code=%d, status=%d, message=%s\n",
		resp.GetExitCode(), resp.GetStatus(), resp.GetMessage())
}

func doDelete(ctx context.Context, client runtimev1.SandboxServiceClient, id string, timeout int64) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	fmt.Printf("删除容器 %s (timeout=%ds)...\n", id, timeout)
	_, err := client.Delete(ctx, &runtimev1.DeleteRequest{Id: id, Timeout: timeout})
	if err != nil {
		log.Fatalf("Delete 失败: %v", err)
	}
	fmt.Println("容器已删除")
}

func doRegister(ctx context.Context, client runtimev1.SandboxServiceClient, opts registerOptions) {
	id := opts.id
	if id == "" {
		id = fmt.Sprintf("reg-%d", time.Now().UnixNano()%1000000)
	}
	var command []string
	if opts.commandLine != "" {
		command = strings.Fields(opts.commandLine)
	}
	runtimeEnvs := make(map[string]string)
	if opts.envs != "" {
		for _, e := range strings.Split(opts.envs, ",") {
			kv := strings.SplitN(e, "=", 2)
			if len(kv) == 2 {
				runtimeEnvs[kv[0]] = kv[1]
			}
		}
	}

	req := &runtimev1.SandboxRegisterRequest{
		Templates: []*runtimev1.SandboxTemplate{
			{
				Id:      id,
				Runtime: opts.image,
				Rootfs: &runtimev1.RootfsConfig{
					Type:   runtimev1.RootfsSrcType_IMAGE,
					Source: &runtimev1.RootfsConfig_ImageUrl{ImageUrl: opts.image},
				},
				Command: command,
				Envs:    runtimeEnvs,
			},
		},
	}
	resp, err := client.Register(ctx, req)
	if err != nil {
		log.Fatalf("Register 失败: %v", err)
	}
	fmt.Printf("注册结果: success=%v, message=%s\n", resp.GetSuccess(), resp.GetMessage())
}

func doUnregister(ctx context.Context, client runtimev1.SandboxServiceClient, id string) {
	if id == "" {
		log.Fatal("--id 参数必填")
	}
	ids := strings.Split(id, ",")
	resp, err := client.Unregister(ctx, &runtimev1.SandboxUnregisterRequest{Ids: ids})
	if err != nil {
		log.Fatalf("Unregister 失败: %v", err)
	}
	fmt.Printf("注销结果: success=%v, message=%s\n", resp.GetSuccess(), resp.GetMessage())
}

func doListSandboxes(ctx context.Context, client runtimev1.SandboxServiceClient) {
	resp, err := client.List(ctx, &runtimev1.ListSandboxesRequest{})
	if err != nil {
		log.Fatalf("List 失败: %v", err)
	}
	sandboxes := resp.GetSandboxes()
	if len(sandboxes) == 0 {
		fmt.Println("无运行中的 sandbox")
		return
	}
	fmt.Printf("sandboxes（共 %d 个）:\n", len(sandboxes))
	for i, sb := range sandboxes {
		fmt.Printf("  [%d] id=%s, runtime=%s, state=%s, exit_code=%d, command=%v\n",
			i+1, sb.GetId(), sb.GetRuntime(), sb.GetState().String(), sb.GetExitCode(), sb.GetCommand())
	}
}

func doListRegistered(ctx context.Context, client runtimev1.SandboxServiceClient) {
	resp, err := client.GetRegistered(ctx, &runtimev1.SandboxGetRegisteredRequest{})
	if err != nil {
		log.Fatalf("GetRegistered 失败: %v", err)
	}
	templates := resp.GetTemplates()
	if len(templates) == 0 {
		fmt.Println("无已注册的 sandbox template")
		return
	}
	fmt.Printf("已注册的 sandbox templates（共 %d 个）:\n", len(templates))
	for i, t := range templates {
		fmt.Printf("  [%d] id=%s, runtime=%s, makeSeed=%v, command=%v\n",
			i+1, t.GetId(), t.GetRuntime(), t.GetMakeSeed(), t.GetCommand())
	}
}
