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

// Package log is common logger client
package log

import (
	"fmt"
	"meta_service/common/logger/config"
	"meta_service/common/logger/zap"
	"path/filepath"
	"sync"

	"github.com/asaskevich/govalidator/v11"
	uberZap "go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

var (
	once             sync.Once
	formatLogger     FormatLogger
	defaultLogger, _ = uberZap.NewProduction()
)

// InitRunLog init run log with log.json file
func InitRunLog(configFile, fileName string) error {
	coreInfo, err := config.GetCoreInfo(configFile)
	if err != nil {
		return err
	}
	if coreInfo.Disable {
		return nil
	}
	formatLogger, err = newFormatLogger(fileName, coreInfo)
	return err
}

// InitRunLogByParam init run log with log.json file
func InitRunLogByParam(configFile string, fileName string) error {
	coreInfo, err := config.GetCoreInfoByParam(configFile)
	if err != nil {
		return err
	}
	if coreInfo.Disable {
		return nil
	}
	formatLogger, err = newFormatLogger(fileName, coreInfo)
	return err
}

// InitRunLogWithConfig init run log with config
func InitRunLogWithConfig(fileName string, coreInfo config.CoreInfo) (FormatLogger, error) {
	if _, err := govalidator.ValidateStruct(coreInfo); err != nil {
		return nil, err
	}
	return newFormatLogger(fileName, coreInfo)
}

// FormatLogger format logger interface
type FormatLogger interface {
	With(fields ...zapcore.Field) FormatLogger

	Infof(format string, paras ...interface{})
	Errorf(format string, paras ...interface{})
	Warnf(format string, paras ...interface{})
	Debugf(format string, paras ...interface{})
	Fatalf(format string, paras ...interface{})

	Info(msg string, fields ...uberZap.Field)
	Error(msg string, fields ...uberZap.Field)
	Warn(msg string, fields ...uberZap.Field)
	Debug(msg string, fields ...uberZap.Field)
	Fatal(msg string, fields ...uberZap.Field)

	Sync()
}

// zapLoggerWithFormat define logger
type zapLoggerWithFormat struct {
	Logger  *uberZap.Logger
	SLogger *uberZap.SugaredLogger
}

// newFormatLogger new formatLogger with log config info
func newFormatLogger(fileName string, coreInfo config.CoreInfo) (FormatLogger, error) {
	coreInfo.FilePath = filepath.Join(coreInfo.FilePath, fileName+"-run.log")
	logger, err := zap.NewWithLevel(coreInfo)
	if err != nil {
		return nil, err
	}

	return &zapLoggerWithFormat{
		Logger:  logger,
		SLogger: logger.Sugar(),
	}, nil
}

// NewConsoleLogger returns a console logger
func NewConsoleLogger() FormatLogger {
	logger, err := zap.NewConsoleLog()
	if err != nil {
		fmt.Println("new console log error", err)
		logger = defaultLogger
	}
	return &zapLoggerWithFormat{
		Logger:  logger,
		SLogger: logger.Sugar(),
	}
}

// GetLogger get logger directly
func GetLogger() FormatLogger {
	if formatLogger == nil {
		once.Do(func() {
			formatLogger = NewConsoleLogger()
		})
	}
	return formatLogger
}

// With add fields to log header
func (z *zapLoggerWithFormat) With(fields ...zapcore.Field) FormatLogger {
	logger := z.Logger.With(fields...)
	return &zapLoggerWithFormat{
		Logger:  logger,
		SLogger: logger.Sugar(),
	}
}

// Infof stdout format and paras
func (z *zapLoggerWithFormat) Infof(format string, paras ...interface{}) {
	z.SLogger.Infof(format, paras...)
}

// Errorf stdout format and paras
func (z *zapLoggerWithFormat) Errorf(format string, paras ...interface{}) {
	z.SLogger.Errorf(format, paras...)
}

// Warnf stdout format and paras
func (z *zapLoggerWithFormat) Warnf(format string, paras ...interface{}) {
	z.SLogger.Warnf(format, paras...)
}

// Debugf stdout format and paras
func (z *zapLoggerWithFormat) Debugf(format string, paras ...interface{}) {
	z.SLogger.Debugf(format, paras...)
}

// Fatalf stdout format and paras
func (z *zapLoggerWithFormat) Fatalf(format string, paras ...interface{}) {
	z.SLogger.Fatalf(format, paras...)
}

// Info stdout format and paras
func (z *zapLoggerWithFormat) Info(msg string, fields ...uberZap.Field) {
	z.Logger.Info(msg, fields...)
}

// Error stdout format and paras
func (z *zapLoggerWithFormat) Error(msg string, fields ...uberZap.Field) {
	z.Logger.Error(msg, fields...)
}

// Warn stdout format and paras
func (z *zapLoggerWithFormat) Warn(msg string, fields ...uberZap.Field) {
	z.Logger.Warn(msg, fields...)
}

// Debug stdout format and paras
func (z *zapLoggerWithFormat) Debug(msg string, fields ...uberZap.Field) {
	z.Logger.Debug(msg, fields...)
}

// Fatal stdout format and paras
func (z *zapLoggerWithFormat) Fatal(msg string, fields ...uberZap.Field) {
	z.Logger.Fatal(msg, fields...)
}

// Sync calls the underlying Core's Sync method, flushing any buffered log
// entries. Applications should take care to call Sync before exiting.
func (z *zapLoggerWithFormat) Sync() {
	z.Logger.Sync()
}
