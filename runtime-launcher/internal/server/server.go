package server

import (
	"fmt"
	"log"
	"net"
	"os"

	pb "runtime-launcher/api/proto/runtime/v1"

	"google.golang.org/grpc"
)

// Server 封装 gRPC 服务器，监听 Unix Domain Socket。
type Server struct {
	grpcServer *grpc.Server
	listener   net.Listener
	socketPath string
}

// New 创建并初始化 gRPC 服务器。
// socketPath 为 UDS 文件路径，launcher 为 RuntimeLauncher 服务实现。
func New(socketPath string, launcher pb.RuntimeLauncherServer) (*Server, error) {
	// 清理可能残留的旧 socket 文件
	if err := os.RemoveAll(socketPath); err != nil {
		return nil, fmt.Errorf("清理旧 socket 文件失败: %w", err)
	}

	lis, err := net.Listen("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("监听 %s 失败: %w", socketPath, err)
	}

	// 设置 socket 文件权限，允许同组用户连接
	if err := os.Chmod(socketPath, 0660); err != nil {
		lis.Close()
		return nil, fmt.Errorf("设置 socket 权限失败: %w", err)
	}

	grpcServer := grpc.NewServer(
		grpc.MaxRecvMsgSize(64*1024*1024), // 64MB 接收消息限制
		grpc.MaxSendMsgSize(64*1024*1024), // 64MB 发送消息限制
	)
	pb.RegisterRuntimeLauncherServer(grpcServer, launcher)

	log.Printf("[server] gRPC 服务器已创建，socket: %s", socketPath)
	return &Server{
		grpcServer: grpcServer,
		listener:   lis,
		socketPath: socketPath,
	}, nil
}

// Run 启动 gRPC 服务器，阻塞直到停止。
func (s *Server) Run() error {
	log.Printf("[server] 开始服务...")
	return s.grpcServer.Serve(s.listener)
}

// GracefulStop 优雅关闭服务器，等待进行中的 RPC 完成。
func (s *Server) GracefulStop() {
	log.Printf("[server] 正在优雅关闭...")
	s.grpcServer.GracefulStop()
	os.Remove(s.socketPath)
	log.Printf("[server] 已关闭")
}
