package logger

import (
	"github.com/stretchr/testify/assert"
	"math"
	"testing"
	"time"

	"github.com/stretchr/testify/suite"
	"go.uber.org/zap/zapcore"
)

type CustomEncoderTestSuite struct {
	suite.Suite
	encoder *customEncoder
}

func (ie *CustomEncoderTestSuite) SetupSuite() {
	cfg := &zapcore.EncoderConfig{
		MessageKey: "test",
	}
	ie.encoder = NewCustomEncoder(cfg).(*customEncoder)
}

func (ie *CustomEncoderTestSuite) TearDownSuite() {
	ie.encoder = nil
}

func TestCustomEncoderTestSuite(t *testing.T) {
	suite.Run(t, new(CustomEncoderTestSuite))
}

func (ie *CustomEncoderTestSuite) TestAppendTime() {
	encoder := ie.encoder

	encoder.AddTime("test", time.Now())
	encoder.AddDuration("test", time.Second)
}

func (ie *CustomEncoderTestSuite) TestAppendObject() {
	encoder := ie.encoder
	encoder.AddArray("test", nil)
	encoder.AddBinary("test", nil)
	encoder.AddObject("test", nil)
	encoder.AddReflected("test", nil)
}

func (ie *CustomEncoderTestSuite) TestAppendString() {
	encoder := ie.encoder
	encoder.AppendString("")
	encoder.AppendByteString(make([]byte, 0))

	encoder.AddString("test", " ")
	encoder.AddByteString("test", make([]byte, 0))
}

func (ie *CustomEncoderTestSuite) TestAppendBool() {
	encoder := ie.encoder
	encoder.AppendBool(false)

	encoder.AddBool("test", false)
}

func (ie *CustomEncoderTestSuite) TestAppendInt() {
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

func (ie *CustomEncoderTestSuite) TestAppendFloat() {
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

func (ie *CustomEncoderTestSuite) TestAppendComplex() {
	encoder := ie.encoder
	encoder.AppendComplex64(0)
	encoder.AppendComplex128(0)

	encoder.AddComplex64("test", 0)
	encoder.AddComplex128("test", 0)
}

func (ie *CustomEncoderTestSuite) TestAppendPtr() {
	encoder := ie.encoder
	encoder.AppendUintptr(0)

	encoder.AddUintptr("test", 0)
}

func (ie *CustomEncoderTestSuite) TestClone() {
	encoder := ie.encoder
	encoder.buf.AppendByte('1')
	encoder.Clone()
	assert.NotNil(ie.T(), encoder)
}

func (ie *CustomEncoderTestSuite) TestEncodeEntry() {
	encoder := ie.encoder
	encoder.podName = "test"
	encoder.buf.AppendByte('1')
	encoder.EncodeCaller = func(caller zapcore.EntryCaller, encoder zapcore.PrimitiveArrayEncoder) {
		return
	}
	encoder.EncodeLevel = func(zapcore.Level, zapcore.PrimitiveArrayEncoder) {
		return
	}
	encoder.StacktraceKey = "test"

	ent := zapcore.Entry{}
	ent.Caller.Defined = true
	ent.Stack = "test"

	fields := make([]zapcore.Field, 0)
	field := zapcore.Field{Key: "", Type: zapcore.StringType}
	fields = append(fields, field)

	_, err := encoder.EncodeEntry(ent, fields)
	assert.Nil(ie.T(), err)
}
