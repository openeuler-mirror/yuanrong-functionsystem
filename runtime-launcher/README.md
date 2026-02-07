# RuntimeLauncher gRPC 服务

RuntimeLauncher 是 yuanrong-functionsystem 的容器运行时启动服务，实现了 `runtime.v1.RuntimeLauncher` gRPC 接口，通过 Unix Domain Socket 与 C++ 侧的 `ContainerExecutor` 通信。

支持 **Docker** 和 **Podman** 两种容器运行时后端。

## 构建

在 lwy1 容器中执行：

```bash
source /etc/profile.d/buildtools.sh
cd /home/robbluo/code/yuanrong-functionsystem/runtime-launcher

# 构建服务端
go build -buildvcs=false -o bin/runtime-launcher ./cmd/runtime-launcher/

# 构建测试客户端
go build -buildvcs=false -o bin/rl-client ./cmd/rl-client/
```

## 使用方式

### Docker 后端（默认）

```bash
./runtime-launcher --backend docker --socket /var/run/runtime-launcher.sock
```

### Podman 后端

```bash
./runtime-launcher --backend podman --socket /var/run/runtime-launcher.sock
```

### 自定义 Docker daemon 地址

```bash
./runtime-launcher --backend docker --docker-host unix:///var/run/docker.sock
```

### 自定义 Podman socket 地址

```bash
./runtime-launcher --backend podman --podman-socket unix:///run/podman/podman.sock
```

## 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--backend` | `docker` | 容器运行时后端：`docker` 或 `podman` |
| `--socket` | `/var/run/runtime-launcher.sock` | gRPC 服务监听的 UDS 路径 |
| `--docker-host` | 空（使用环境变量 `DOCKER_HOST`） | Docker daemon 地址（仅 docker 后端） |
| `--podman-socket` | `unix:///run/podman/podman.sock` | Podman socket 地址（仅 podman 后端） |

## gRPC 接口

服务定义于 `api/proto/runtime/v1/runtime_launcher.proto`：

| RPC | 说明 |
|-----|------|
| `Start` | 创建并启动容器，返回容器 ID |
| `Wait` | 阻塞等待容器退出，返回退出码 |
| `Delete` | 停止并删除容器（支持优雅超时） |
| `Register` | 将运行时注册到预热表 |
| `Unregister` | 从预热表中移除 |
| `GetRegistered` | 查询所有已注册的运行时 |

## 测试客户端 (rl-client)

`rl-client` 是配套的 gRPC 测试客户端，用于验证 RuntimeLauncher 服务。使用前需先启动服务端。

### 完整生命周期（run = start + wait + delete）

```bash
# 启动 alpine 执行 echo hello，等待退出后自动删除
rl-client --image alpine:latest --cmd "echo hello"

# 带挂载目录
rl-client --image alpine:latest --cmd "ls /data" --mount /tmp:/data

# 带只读挂载
rl-client --image alpine:latest --cmd "cat /conf/app.yaml" --mount /etc/myapp:/conf:ro

# 带环境变量
rl-client --image alpine:latest --cmd "env" --env "FOO=bar,APP=test"

# 自定义资源限制
rl-client --image alpine:latest --cmd "echo hi" --cpu 1000 --mem 256

# 多个挂载 + 环境变量
rl-client --image python:3.11-slim --cmd "python /app/main.py" \
  --mount /home/code:/app,/tmp/data:/data:ro \
  --env "PYTHONPATH=/app,DEBUG=1"
```

### 分步操作

```bash
# 仅启动容器（返回容器 ID）
rl-client --action start --image alpine:latest --cmd "sleep 60"

# 等待指定容器退出
rl-client --action wait --id <容器ID>

# 删除容器（优雅超时 10 秒）
rl-client --action delete --id <容器ID> --timeout 10
```

### 预热注册管理

```bash
# 注册运行时到预热表
rl-client --action register --image python:3.11-slim --id my-runtime --cmd "python main.py"

# 查看所有已注册运行时
rl-client --action list

# 注销运行时
rl-client --action unregister --id my-runtime
```

### rl-client 参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--action` | `run` | 操作类型：`run`、`start`、`wait`、`delete`、`register`、`unregister`、`list` |
| `--image` | 空 | 容器镜像（`run`/`start`/`register` 必填） |
| `--cmd` | 空 | 容器内执行的命令 |
| `--mount` | 空 | 挂载，格式 `源:目标[:ro]`，多个用逗号分隔 |
| `--env` | 空 | 环境变量，格式 `KEY=VAL`，多个用逗号分隔 |
| `--id` | 空 | 容器/运行时 ID（`wait`/`delete`/`unregister` 必填） |
| `--socket` | `/var/run/runtime-launcher.sock` | 服务端 UDS 路径 |
| `--cpu` | `500` | CPU 毫核 |
| `--mem` | `512` | 内存 MB |
| `--timeout` | `5` | 删除时优雅超时秒数 |

## 项目结构

```
runtime-launcher/
├── cmd/
│   ├── runtime-launcher/main.go          # 服务端入口
│   └── rl-client/main.go                 # 测试客户端
├── api/proto/runtime/v1/                 # proto 定义及生成代码
├── internal/
│   ├── server/server.go                  # gRPC 服务器（UDS 监听、优雅关闭）
│   ├── service/launcher.go               # 6 个 RPC 方法实现
│   ├── runtime/
│   │   ├── runtime.go                    # ContainerRuntime 接口 + 工厂
│   │   ├── docker.go                     # Docker 后端
│   │   └── podman.go                     # Podman 后端
│   └── state/manager.go                  # 容器状态 + 预热注册表管理
├── go.mod
└── go.sum
```

## 与 C++ 客户端对接

C++ 侧的 `ContainerExecutor` 通过环境变量 `CONTAINER_EP` 指定 socket 路径连接本服务：

```bash
export CONTAINER_EP=/var/run/runtime-launcher.sock
```
