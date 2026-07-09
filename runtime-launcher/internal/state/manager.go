package state

import (
	"sync"
	"time"

	"runtime-launcher/api/proto/runtime/v1"
	"runtime-launcher/internal/runtime"
)

// ContainerState 跟踪运行中 sandbox/container 的元数据。
type ContainerState struct {
	ID          string
	RuntimeID   string
	ExitCode    int32
	ExitMessage string
	Exited      bool
	StartedAt   int64
	FinishedAt  int64
	Config      *runtime.CreateConfig
	DoneCh      chan struct{}
}

// Manager 提供线程安全的 sandbox 和预热模板状态管理。
type Manager struct {
	mu         sync.RWMutex
	containers map[string]*ContainerState

	regMu      sync.RWMutex
	registered map[string]*runtimev1.SandboxTemplate
}

// NewManager 创建新的状态管理器。
func NewManager() *Manager {
	return &Manager{
		containers: make(map[string]*ContainerState),
		registered: make(map[string]*runtimev1.SandboxTemplate),
	}
}

// AddContainer 记录一个新启动的 sandbox/container。
func (m *Manager) AddContainer(containerID, runtimeID string, cfg *runtime.CreateConfig) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.containers[containerID] = &ContainerState{
		ID:        containerID,
		RuntimeID: runtimeID,
		StartedAt: time.Now().Unix(),
		Config:    cfg,
		DoneCh:    make(chan struct{}),
	}
}

// GetContainer 查找容器状态，并返回锁内快照。
func (m *Manager) GetContainer(containerID string) (*ContainerState, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	cs, ok := m.containers[containerID]
	if !ok {
		return nil, false
	}
	copyState := *cs
	return &copyState, true
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
	cs.FinishedAt = time.Now().Unix()
	close(cs.DoneCh)
}

// RemoveContainer 从状态中移除容器。
func (m *Manager) RemoveContainer(containerID string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	cs, ok := m.containers[containerID]
	if ok && !cs.Exited {
		cs.Exited = true
		cs.FinishedAt = time.Now().Unix()
		close(cs.DoneCh)
	}
	delete(m.containers, containerID)
}

// ListContainers 返回当前跟踪的 sandbox/container 状态。
func (m *Manager) ListContainers(id string) []*ContainerState {
	m.mu.RLock()
	defer m.mu.RUnlock()
	result := make([]*ContainerState, 0, len(m.containers))
	for _, cs := range m.containers {
		if id != "" && cs.ID != id {
			continue
		}
		copyState := *cs
		result = append(result, &copyState)
	}
	return result
}

// RegisterTemplate 将 SandboxTemplate 加入预热注册表。
func (m *Manager) RegisterTemplate(t *runtimev1.SandboxTemplate) {
	if t == nil {
		return
	}
	m.regMu.Lock()
	defer m.regMu.Unlock()
	m.registered[t.GetId()] = t
}

// UnregisterTemplate 从预热注册表中移除。
func (m *Manager) UnregisterTemplate(id string) {
	m.regMu.Lock()
	defer m.regMu.Unlock()
	delete(m.registered, id)
}

// GetRegisteredTemplate 查找单个注册模板。
func (m *Manager) GetRegisteredTemplate(id string) (*runtimev1.SandboxTemplate, bool) {
	m.regMu.RLock()
	defer m.regMu.RUnlock()
	t, ok := m.registered[id]
	return t, ok
}

// GetAllRegisteredTemplates 返回所有已注册模板。
func (m *Manager) GetAllRegisteredTemplates() []*runtimev1.SandboxTemplate {
	m.regMu.RLock()
	defer m.regMu.RUnlock()
	result := make([]*runtimev1.SandboxTemplate, 0, len(m.registered))
	for _, t := range m.registered {
		result = append(result, t)
	}
	return result
}
