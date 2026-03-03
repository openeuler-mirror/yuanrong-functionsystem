#!/bin/bash
# RuntimeLauncher 端到端测试脚本
# 用法: ./test.sh [镜像名称]
# 示例: ./test.sh alpine:latest
#       ./test.sh swr.cn-southwest-2.myhuaweicloud.com/yuanrong-dev/compile_x86:2.1

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_BIN="$SCRIPT_DIR/bin/runtime-launcher"
CLIENT_BIN="$SCRIPT_DIR/bin/rl-client"
SOCKET="/tmp/runtime-launcher-test.sock"
IMAGE="${1:-swr.cn-southwest-2.myhuaweicloud.com/yuanrong-dev/compile_x86:2.1}"
SERVER_PID=""
PASSED=0
FAILED=0
TOTAL=0

pick_free_port() {
    for p in $(seq 40080 40120); do
        if ! ss -lnt 2>/dev/null | awk '{print $4}' | grep -qE "[:.]${p}$"; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

# 颜色
GREEN="\033[32m"
RED="\033[31m"
YELLOW="\033[33m"
RESET="\033[0m"

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo ""
        echo "--- 清理: 停止服务端 (PID=$SERVER_PID) ---"
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SOCKET" /tmp/rl-test-file.txt
}
trap cleanup EXIT

run_test() {
    local name="$1"
    shift
    TOTAL=$((TOTAL + 1))
    echo ""
    echo -e "${YELLOW}[$TOTAL] $name${RESET}"
    if OUTPUT=$("$@" 2>&1); then
        echo "$OUTPUT"
        echo -e "${GREEN}  => 通过${RESET}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo "$OUTPUT"
        echo -e "${RED}  => 失败 (exit=$?)${RESET}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# ============================
# 检查二进制文件
# ============================
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "二进制文件不存在，请先构建:"
    echo "  go build -buildvcs=false -o bin/runtime-launcher ./cmd/runtime-launcher/"
    echo "  go build -buildvcs=false -o bin/rl-client ./cmd/rl-client/"
    exit 1
fi

echo "========================================"
echo " RuntimeLauncher 端到端测试"
echo "========================================"
echo "  服务端:  $SERVER_BIN"
echo "  客户端:  $CLIENT_BIN"
echo "  Socket:  $SOCKET"
echo "  镜像:    $IMAGE"
echo "========================================"

# ============================
# 启动服务端
# ============================
echo ""
echo "--- 启动服务端 ---"
rm -f "$SOCKET"
"$SERVER_BIN" --backend docker --socket "$SOCKET" &
SERVER_PID=$!
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${RED}服务端启动失败${RESET}"
    exit 1
fi
echo "服务端已启动 (PID=$SERVER_PID)"

# ============================
# 测试 1: Register
# ============================
run_test "Register — 注册运行时" \
    "$CLIENT_BIN" --socket "$SOCKET" --action register \
    --image "$IMAGE" --id test-rt-1 --cmd "echo hello"

# ============================
# 测试 2: List（应有 1 条）
# ============================
run_test "List — 查询已注册运行时（应有 1 条）" \
    "$CLIENT_BIN" --socket "$SOCKET" --action list

# ============================
# 测试 3: Unregister
# ============================
run_test "Unregister — 注销运行时" \
    "$CLIENT_BIN" --socket "$SOCKET" --action unregister --id test-rt-1

# ============================
# 测试 4: List（应为空）
# ============================
run_test "List — 查询已注册运行时（应为空）" \
    "$CLIENT_BIN" --socket "$SOCKET" --action list

# ============================
# 测试 5: Run 基本命令
# ============================
run_test "Run — 基本命令 (echo hello)" \
    "$CLIENT_BIN" --socket "$SOCKET" --action run \
    --image "$IMAGE" --cmd "echo hello-from-runtime-launcher"

# ============================
# 测试 6: Run 带挂载
# ============================
echo "test-content-12345" > /tmp/rl-test-file.txt
run_test "Run — 带挂载 (mount 文件)" \
    "$CLIENT_BIN" --socket "$SOCKET" --action run \
    --image "$IMAGE" --cmd "cat /mnt/test-file.txt" \
    --mount /tmp/rl-test-file.txt:/mnt/test-file.txt

# ============================
# 测试 7: Run 带环境变量
# ============================
run_test "Run — 带环境变量" \
    "$CLIENT_BIN" --socket "$SOCKET" --action run \
    --image "$IMAGE" --cmd "echo MY_VAR=\$MY_VAR APP=\$APP" \
    --env "MY_VAR=hello_world,APP=runtime-launcher"

# ============================
# 测试 8: Run 自定义资源
# ============================
run_test "Run — 自定义资源限制 (cpu=1000, mem=256)" \
    "$CLIENT_BIN" --socket "$SOCKET" --action run \
    --image "$IMAGE" --cmd "echo resource-test-ok" \
    --cpu 1000 --mem 256

# ============================
# 测试 9: 分步 Start
# ============================
echo ""
TOTAL=$((TOTAL + 1))
echo -e "${YELLOW}[$TOTAL] 分步操作 — Start + Wait + Delete${RESET}"
START_OUT=$("$CLIENT_BIN" --socket "$SOCKET" --action start \
    --image "$IMAGE" --cmd "sleep 2 && echo step-done" 2>&1)
echo "$START_OUT"
CID=$(echo "$START_OUT" | grep "容器已启动" | sed 's/.*id=//')

if [ -z "$CID" ]; then
    echo -e "${RED}  => Start 失败，未获取到容器 ID${RESET}"
    FAILED=$((FAILED + 1))
else
    # Wait
    echo ""
    echo "  Wait: 等待容器退出..."
    WAIT_OUT=$("$CLIENT_BIN" --socket "$SOCKET" --action wait --id "$CID" 2>&1)
    echo "  $WAIT_OUT"

    # Delete
    echo "  Delete: 删除容器..."
    DEL_OUT=$("$CLIENT_BIN" --socket "$SOCKET" --action delete --id "$CID" 2>&1)
    echo "  $DEL_OUT"

    if echo "$WAIT_OUT" | grep -q "exit_code=0"; then
        echo -e "${GREEN}  => 通过${RESET}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}  => 失败（exit_code 不为 0）${RESET}"
        FAILED=$((FAILED + 1))
    fi
fi

# ============================
# 测试 10: 端口转发（桥接网络）
# ============================
echo ""
TOTAL=$((TOTAL + 1))
echo -e "${YELLOW}[$TOTAL] 端口转发 — Start + Docker PortBinding 校验${RESET}"

HOST_PORT=$(pick_free_port || true)
if [ -z "$HOST_PORT" ]; then
    echo -e "${RED}  => 失败：未找到可用测试端口${RESET}"
    FAILED=$((FAILED + 1))
else
    START_PORT_OUT=$("$CLIENT_BIN" --socket "$SOCKET" --action start \
        --image "$IMAGE" --cmd "sleep 20" --network bridge \
        --ports "tcp:${HOST_PORT}:8080" 2>&1)
    echo "$START_PORT_OUT"
    PORT_CID=$(echo "$START_PORT_OUT" | grep "容器已启动" | sed 's/.*id=//')

    if [ -z "$PORT_CID" ]; then
        echo -e "${RED}  => 失败：启动失败，未获取到容器 ID${RESET}"
        FAILED=$((FAILED + 1))
    else
        PORT_BIND=$(docker inspect "$PORT_CID" --format '{{(index (index .HostConfig.PortBindings "8080/tcp") 0).HostPort}}' 2>/dev/null || true)

        # 清理容器
        "$CLIENT_BIN" --socket "$SOCKET" --action delete --id "$PORT_CID" >/dev/null 2>&1 || true

        if [ "$PORT_BIND" = "$HOST_PORT" ]; then
            echo "  docker inspect port binding: 8080/tcp -> ${PORT_BIND}"
            echo -e "${GREEN}  => 通过${RESET}"
            PASSED=$((PASSED + 1))
        else
            echo "  期望端口绑定: ${HOST_PORT}, 实际: ${PORT_BIND}"
            echo -e "${RED}  => 失败：端口映射未生效${RESET}"
            FAILED=$((FAILED + 1))
        fi
    fi
fi

# ============================
# 结果汇总
# ============================
echo ""
echo "========================================"
echo " 测试结果"
echo "========================================"
echo -e "  总计:  $TOTAL"
echo -e "  ${GREEN}通过:  $PASSED${RESET}"
if [ "$FAILED" -gt 0 ]; then
    echo -e "  ${RED}失败:  $FAILED${RESET}"
else
    echo -e "  失败:  0"
fi
echo "========================================"

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
