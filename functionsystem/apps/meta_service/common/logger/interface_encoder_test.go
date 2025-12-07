/*
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
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

package logger

import (
	"math"
	"os"
	"testing"
	"time"

	"meta_service/common/logger/config"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/suite"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

// TestNewInterfaceEncoder Test New Interface Encoder
func TestNewInterfaceEncoder(t *testing.T) {
	cfg := InterfaceEncoderConfig{
		ModuleName: "FunctionWorker",
		HTTPMethod: "POST",
		ModuleFrom: "FrontendInvoke",
		TenantID:   "tenant2",
		FuncName:   "myFunction",
		FuncVer:    "latest",
	}

	encoder := NewInterfaceEncoder(cfg, false)

	priority := zap.LevelEnablerFunc(func(lvl zapcore.Level) bool {
		return lvl >= zapcore.InfoLevel
	})

	sink := zapcore.Lock(os.Stdout)
	core := zapcore.NewCore(encoder, sink, priority)
	logger := zap.New(core)

	logger.Info("e1b71add-cb24-4ef8-93eb-af8d3ceb74e8|0|success|1")
}

func Test_newCore(t *testing.T) {
	coreInfo := config.CoreInfo{}
	cfg := InterfaceEncoderConfig{}
	_, err := newCore(coreInfo, cfg)
	assert.NotNil(t, err)
}

type InterfaceEncoderTestSuite struct {
	suite.Suite
	encoder *interfaceEncoder
}

func (ie *InterfaceEncoderTestSuite) SetupSuite() {
	cfg := InterfaceEncoderConfig{
		ModuleName: "FunctionWorker",
		HTTPMethod: "POST",
		ModuleFrom: "FrontendInvoke",
		TenantID:   "tenant2",
		FuncName:   "myFunction",
		FuncVer:    "latest",
	}
	ie.encoder = newInterfaceEncoder(cfg, false)
}

func (ie *InterfaceEncoderTestSuite) TearDownSuite() {
	ie.encoder = nil
}

func TestInterfaceEncoderTestSuite(t *testing.T) {
	suite.Run(t, new(InterfaceEncoderTestSuite))
}

func (ie *InterfaceEncoderTestSuite) TestClone() {
	encoder := ie.encoder.Clone()
	assert.NotNil(ie.T(), encoder)
}

func (ie *InterfaceEncoderTestSuite) TestOpenNameSpace() {
	encoder := ie.encoder
	encoder.OpenNamespace("test")
	assert.NotNil(ie.T(), encoder)
}

func (ie *InterfaceEncoderTestSuite) TestEncodeEntry() {
	encoder := ie.encoder
	encoder.podName = "test"
	encoder.buf.AppendByte('1')
	encoder.EncodeCaller = func(caller zapcore.EntryCaller, encoder zapcore.PrimitiveArrayEncoder) {
		return
	}

	ent := zapcore.Entry{}
	ent.Caller.Defined = true

	fields := make([]zapcore.Field, 0)
	field := zapcore.Field{Key: "", Type: zapcore.StringType}
	fields = append(fields, field)

	_, err := encoder.EncodeEntry(ent, fields)
	assert.Nil(ie.T(), err)
}

func (ie *InterfaceEncoderTestSuite) TestAddElementSeparator() {
	encoder := ie.encoder
	encoder.addElementSeparator()

	encoder.buf.AppendByte(' ')
	encoder.addElementSeparator()

	encoder.buf.AppendByte('a')
	encoder.spaced = true
	encoder.addElementSeparator()
}

func (ie *InterfaceEncoderTestSuite) TestInterfaceTimeEncode() {
	encoder := ie.encoder
	interfaceTimeEncode(time.Now(), encoder)
}

func (ie *InterfaceEncoderTestSuite) TestAppendTime() {
	encoder := ie.encoder
	encoder.AppendTime(time.Now())
	encoder.AppendDuration(time.Second)

	encoder.AddTime("test", time.Now())
	encoder.AddDuration("test", time.Second)
}

func (ie *InterfaceEncoderTestSuite) TestAppendObject() {
	encoder := ie.encoder
	encoder.AddArray("test", nil)
	encoder.AddBinary("test", nil)
	encoder.AddObject("test", nil)
	encoder.AddReflected("test", nil)
}

func (ie *InterfaceEncoderTestSuite) TestAppendString() {
	encoder := ie.encoder
	encoder.AppendString("")
	encoder.AppendByteString(make([]byte, 0))

	encoder.AddString("test", "")
	encoder.AddByteString("test", make([]byte, 0))
}

func (ie *InterfaceEncoderTestSuite) TestAppendBool() {
	encoder := ie.encoder
	encoder.AppendBool(false)

	encoder.AddBool("test", false)
}

func (ie *InterfaceEncoderTestSuite) TestAppendInt() {
	encoder := ie.encoder
	encoder.AppendInt(0)
	encoder.AppendInt8(0)
	encoder.AppendInt16(0)
	encoder.AppendInt32(0)
	encoder.AppendInt64(0)
	encoder.AppendUint(0)
	encoder.AppendUint8(0)
	encoder.AppendUint16(0)
	encoder.AppendUint32(0)
	encoder.AppendUint64(0)

	encoder.AddInt("test", 0)
	encoder.AddInt8("test", 0)
	encoder.AddInt16("test", 0)
	encoder.AddInt32("test", 0)
	encoder.AddInt64("test", 0)
	encoder.AddUint("test", 0)
	encoder.AddUint8("test", 0)
	encoder.AddUint16("test", 0)
	encoder.AddUint32("test", 0)
	encoder.AddUint64("test", 0)
}

func (ie *InterfaceEncoderTestSuite) TestAppendFloat() {
	encoder := ie.encoder
	encoder.buf.AppendByte(' ')
	encoder.appendFloat(math.NaN(), 64)
	encoder.appendFloat(math.Inf(1), 64)
	encoder.appendFloat(math.Inf(-1), 64)
	encoder.appendFloat(0, 64)
	encoder.AppendFloat32(0)
	encoder.AppendFloat64(0)

	encoder.AddFloat32("test", 0)
	encoder.AddFloat64("test", 0)
}

func (ie *InterfaceEncoderTestSuite) TestAppendComplex() {
	encoder := ie.encoder
	encoder.AppendComplex64(0)
	encoder.AppendComplex128(0)

	encoder.AddComplex64("test", 0)
	encoder.AddComplex128("test", 0)
}

func (ie *InterfaceEncoderTestSuite) TestAppendPtr() {
	encoder := ie.encoder
	encoder.AppendUintptr(0)

	encoder.AddUintptr("test", 0)
}

/*
func (ie *InterfaceEncoderTestSuite) Test() {
encoder := ie.encoder
}
*/
