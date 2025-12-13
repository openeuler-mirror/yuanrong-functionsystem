/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package debug

import (
	"bytes"
	"fmt"
	"io"
	"testing"

	"github.com/spf13/cobra"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"

	"cli/constant"
)

// MockCmdIO 用于模拟 CmdIO 的行为
type MockCmdIO struct {
	mock.Mock
}

// In 返回模拟的输入
func (m *MockCmdIO) In() io.Reader {
	args := m.Called()
	return args.Get(0).(io.Reader)
}

// Out 返回模拟的输出
func (m *MockCmdIO) Out() io.Writer {
	args := m.Called()
	return args.Get(0).(io.Writer)
}

// ErrOut 返回模拟的错误输出
func (m *MockCmdIO) ErrOut() io.Writer {
	args := m.Called()
	return args.Get(0).(io.Writer)
}

// TestInitYrDebug 测试 InitYrDebug 函数
func TestInitYrDebug(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 调用 InitYrDebug 函数
	cmd := InitYrDebug(mockCmdIO)

	// 检查命令是否正确初始化
	assert.NotNil(t, cmd)
	assert.Equal(t, "debug", cmd.Use)
	assert.Equal(t, fmt.Sprintf("Enter the %s debug shell", constant.PlatformName), cmd.Short)
}

// TestYrDebug 测试 yrDebug 函数
func TestYrDebug(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 设置全局 opts 的 cmdIO
	opts.cmdIO = mockCmdIO

	// 调用 yrDebug 函数
	err := yrDebug(&cobra.Command{}, []string{})

	// 检查是否有错误
	assert.NoError(t, err)
}

// TestStartDebugShell 测试 startDebugShell 函数
func TestStartDebugShell(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString("quit\n"))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 设置全局 opts 的 cmdIO
	opts.cmdIO = mockCmdIO

	// 调用 startDebugShell 函数
	startDebugShell()

	// 检查输出内容
	output := mockCmdIO.Out().(*bytes.Buffer).String()
	expectedOutput := "Type 'help' or 'h' for help, 'quit' or 'q' to exit.\n\n"
	assert.Contains(t, output, expectedOutput, "startDebugShell output should contain expected output")
}

// TestHandleHelp 测试 handleHelp 函数
func TestHandleHelp(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 调用 handleHelp 函数
	err := handleHelp(mockCmdIO, []string{"help"})

	// 检查是否有错误
	assert.NoError(t, err)
}

// TestHandleQuit 测试 handleQuit 函数
func TestHandleQuit(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 调用 handleQuit 函数
	err := handleQuit(mockCmdIO, []string{"quit"})

	// 检查是否有错误
	assert.NoError(t, err)
}

// TestHandleInstance 测试 handleInstance 函数
func TestHandleInstance(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 设置全局 opts 的 cmdIO
	opts.cmdIO = mockCmdIO

	// 调用 handleInstance 函数
	err := handleInstance([]string{"instance", "12345"})

	// 检查是否有错误
	assert.NoError(t, err)
}

// TestHandleInfo 测试 handleInfo 函数
func TestHandleInfo(t *testing.T) {
	// 创建一个模拟的 CmdIO
	mockCmdIO := &MockCmdIO{}
	mockCmdIO.On("In").Return(bytes.NewBufferString(""))
	mockCmdIO.On("Out").Return(bytes.NewBuffer(nil))
	mockCmdIO.On("ErrOut").Return(bytes.NewBuffer(nil))

	// 调用 handleInfo 函数
	err := handleInfo(mockCmdIO, []string{"info", "instance"})

	// 检查是否有错误
	assert.NoError(t, err)
}
