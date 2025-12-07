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

// Package zap zapper log
package zap

import (
	"fmt"
	"path/filepath"
	"time"

	"meta_service/common/logger"
	"meta_service/common/logger/config"

	uberZap "go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

const (
	skipLevel = 1
)

// DefaultEncodeConfig is the default EncoderConfig used for zaplog
var DefaultEncodeConfig = zapcore.EncoderConfig{
	TimeKey:      "T",
	LevelKey:     "L",
	NameKey:      "Logger",
	MessageKey:   "M",
	CallerKey:    "C",
	LineEnding:   zapcore.DefaultLineEnding,
	EncodeLevel:  zapcore.CapitalLevelEncoder,
	EncodeTime:   zapcore.ISO8601TimeEncoder,
	EncodeCaller: zapcore.ShortCallerEncoder,
}

func init() {
	uberZap.RegisterEncoder("custom_console", logger.NewConsoleEncoder)
}

// NewDevelopmentLog returns a development logger based on uber zap and it output entry to stdout and stderr
func NewDevelopmentLog() (*uberZap.Logger, error) {
	cfg := uberZap.NewDevelopmentConfig()
	cfg.EncoderConfig.EncodeLevel = zapcore.CapitalColorLevelEncoder
	return cfg.Build()
}

// NewProductionLog returns a product logger based on uber zap
func NewProductionLog(outputPath string) (*uberZap.Logger, error) {
	const pathLen = 1
	outputPaths := make([]string, pathLen, pathLen)
	if outputPath == "" {
		outputPaths = []string{filepath.Join("/", "log", "run.log"), "stdout"}
	} else {
		outputPaths = []string{outputPath, "stdout"}
	}

	cfg := uberZap.Config{
		Level:             uberZap.NewAtomicLevelAt(uberZap.InfoLevel),
		Development:       false,
		DisableCaller:     false,
		DisableStacktrace: true,
		Encoding:          "json",
		OutputPaths:       outputPaths,
		ErrorOutputPaths:  outputPaths,
		EncoderConfig:     DefaultEncodeConfig,
	}

	return cfg.Build()
}

// NewConsoleLog returns a console logger based on uber zap
func NewConsoleLog() (*uberZap.Logger, error) {
	outputPaths := []string{"stdout"}
	cfg := uberZap.Config{
		Level:             uberZap.NewAtomicLevelAt(uberZap.InfoLevel),
		Development:       false,
		DisableCaller:     false,
		DisableStacktrace: true,
		Encoding:          "custom_console",
		OutputPaths:       outputPaths,
		ErrorOutputPaths:  outputPaths,
		EncoderConfig:     DefaultEncodeConfig,
	}
	consoleLogger, err := cfg.Build()
	if err != nil {
		return nil, err
	}
	return consoleLogger.WithOptions(uberZap.AddCaller(), uberZap.AddCallerSkip(skipLevel)), nil
}

// NewWithLevel returns a log based on zap with Level
func NewWithLevel(coreInfo config.CoreInfo) (*uberZap.Logger, error) {
	core, err := newCore(coreInfo)
	if err != nil {
		return nil, err
	}

	return uberZap.New(core, uberZap.AddCaller(), uberZap.AddCallerSkip(skipLevel)), nil
}

func newCore(coreInfo config.CoreInfo) (zapcore.Core, error) {
	w, err := logger.CreateSink(coreInfo)
	if err != nil {
		return nil, err
	}
	syncer := zapcore.AddSync(w)

	encoderConfig := DefaultEncodeConfig

	fileEncoder := logger.NewCustomEncoder(&encoderConfig)
	priority := uberZap.LevelEnablerFunc(func(lvl zapcore.Level) bool {
		var customLevel zapcore.Level
		if err := customLevel.UnmarshalText([]byte(coreInfo.Level)); err != nil {
			customLevel = zapcore.InfoLevel
		}
		return lvl >= customLevel
	})

	if coreInfo.Tick == 0 || coreInfo.First == 0 || coreInfo.Thereafter == 0 {
		return zapcore.NewCore(fileEncoder, syncer, priority), nil
	}
	return zapcore.NewSamplerWithOptions(zapcore.NewCore(fileEncoder, syncer, priority),
		time.Duration(coreInfo.Tick)*time.Second, coreInfo.First, coreInfo.Thereafter), nil
}

// LoggerWithFormat zap logger
type LoggerWithFormat struct {
	*uberZap.Logger
}

// Infof stdout format and paras
func (z *LoggerWithFormat) Infof(format string, paras ...interface{}) {
	z.Logger.Info(fmt.Sprintf(format, paras...))
}

// Errorf stdout format and paras
func (z *LoggerWithFormat) Errorf(format string, paras ...interface{}) {
	z.Logger.Error(fmt.Sprintf(format, paras...))
}

// Warnf stdout format and paras
func (z *LoggerWithFormat) Warnf(format string, paras ...interface{}) {
	z.Logger.Warn(fmt.Sprintf(format, paras...))
}

// Debugf stdout format and paras
func (z *LoggerWithFormat) Debugf(format string, paras ...interface{}) {
	z.Logger.Debug(fmt.Sprintf(format, paras...))
}

// Fatalf stdout format and paras
func (z *LoggerWithFormat) Fatalf(format string, paras ...interface{}) {
	z.Logger.Fatal(fmt.Sprintf(format, paras...))
}
