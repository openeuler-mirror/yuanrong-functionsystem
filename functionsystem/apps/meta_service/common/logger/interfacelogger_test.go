package logger

import (
	"errors"
	"testing"

	"meta_service/common/logger/config"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"
	"go.uber.org/zap/zapcore"
)

func TestNewInterfaceLogger(t *testing.T) {
	cfg := InterfaceEncoderConfig{}
	_, err := NewInterfaceLogger("", "file", cfg)
	assert.Nil(t, err)

	patch1 := gomonkey.ApplyFunc(config.GetCoreInfo, func(string) (config.CoreInfo, error) {
		return config.CoreInfo{}, errors.New("GetDefaultCoreInfo fail")
	})
	_, err = NewInterfaceLogger("", "file", cfg)
	assert.Nil(t, err)
	patch1.Reset()

	patch2 := gomonkey.ApplyFunc(newCore, func(config.CoreInfo, InterfaceEncoderConfig) (zapcore.Core, error) {
		return nil, errors.New("newCore fail")
	})
	_, err = NewInterfaceLogger("", "file", cfg)
	assert.NotNil(t, err)
	patch2.Reset()
}

func TestWrite(*testing.T) {
	cfg := InterfaceEncoderConfig{}
	logger, _ := NewInterfaceLogger("", "file", cfg)
	logger.Write("test")
}
