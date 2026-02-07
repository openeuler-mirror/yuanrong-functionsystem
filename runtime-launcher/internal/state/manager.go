package state

import (
	"sync"

	pb "runtime-launcher/api/proto/runtime/v1"
)

// ContainerState 跟踪运行中容器的元数据。
type ContainerState struct {
	ID          string
	RuntimeID   string // 启动此容器的 FunctionRuntime.id
	ExitCode    int32
	ExitMessage string
	Exited      bool
	DoneCh      chan struct{} // 容器退出时关闭此 channel
}

// Manager 提供线程安全的容器和预热注册状态管理。
type Manager struct {
	mu         sync.RWMutex
	containers map[string]*ContainerState // key: 容器 ID

	regMu      sync.RWMutex
	registered map[string]*pb.FunctionRuntime // key: FunctionRuntime.id（预热注册表）
}

// NewManager 创建新的状态管理器。
func NewManager() *Manager {
	return &Manager{
		containers: make(map[string]*ContainerState),
		registered: make(map[string]*pb.FunctionRuntime),
	}
}

// AddContainer 记录一个新启动的容器。
func (m *Manager) AddContainer(containerID, runtimeID string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.containers[containerID] = &ContainerState{
		ID:        containerID,
		RuntimeID: runtimeID,
		DoneCh:    make(chan struct{}),
	}
}

// GetContainer 查找容器状态。
func (m *Manager) GetContainer(containerID string) (*ContainerState, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	cs, ok := m.containers[containerID]
	return cs, ok
}

// MarkExited 标记容器已退出，关闭 DoneCh 通知所有等待者。
func (m *Manager) MarkExited(containerID string, exitCode int32, message string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	cs, ok := m.containers[containerID]
	if !ok {
		return
	}
	if cs.Exited {
		return
	}
	cs.ExitCode = exitCode
	cs.ExitMessage = message
	cs.Exited = true
	close(cs.DoneCh)
}

// RemoveContainer 从状态中移除容器。
func (m *Manager) RemoveContainer(containerID string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	cs, ok := m.containers[containerID]
	if ok && !cs.Exited {
		cs.Exited = true
		close(cs.DoneCh)
	}
	delete(m.containers, containerID)
}

// RegisterRuntime 将 FunctionRuntime 加入预热注册表。
func (m *Manager) RegisterRuntime(rt *pb.FunctionRuntime) {
	m.regMu.Lock()
	defer m.regMu.Unlock()
	m.registered[rt.GetId()] = rt
}

// UnregisterRuntime 从预热注册表中移除。
func (m *Manager) UnregisterRuntime(id string) {
	m.regMu.Lock()
	defer m.regMu.Unlock()
	delete(m.registered, id)
}

// GetRegisteredRuntime 查找单个注册的运行时。
func (m *Manager) GetRegisteredRuntime(id string) (*pb.FunctionRuntime, bool) {
	m.regMu.RLock()
	defer m.regMu.RUnlock()
	rt, ok := m.registered[id]
	return rt, ok
}

// GetAllRegistered 返回所有已注册的 FunctionRuntime。
func (m *Manager) GetAllRegistered() []*pb.FunctionRuntime {
	m.regMu.RLock()
	defer m.regMu.RUnlock()
	result := make([]*pb.FunctionRuntime, 0, len(m.registered))
	for _, rt := range m.registered {
		result = append(result, rt)
	}
	return result
}
