package log

import (
	"errors"
	"testing"

	"meta_service/common/logger/config"
	"meta_service/common/logger/zap"

	"github.com/agiledragon/gomonkey"
	"github.com/asaskevich/govalidator/v11"
	. "github.com/smartystreets/goconvey/convey"
	uberZap "go.uber.org/zap"
)

func TestNewConsoleLogger(t *testing.T) {
	patch := gomonkey.ApplyFunc(zap.NewConsoleLog, func() (*uberZap.Logger, error) {
		return nil, errors.New("NewConsoleLog fail")
	})
	NewConsoleLogger()
	patch.Reset()
}

func TestInitRunLogByParam(t *testing.T) {
	Convey("test InitRunLogByParam GetCoreInfoByParam fail", t, func() {
		patch := gomonkey.ApplyFunc(config.GetCoreInfoByParam, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{}, errors.New("GetCoreInfoByParam fail")
		})
		err := InitRunLogByParam("", "")
		So(err, ShouldNotBeNil)
		patch.Reset()
	})
	Convey("test InitRunLogByParam GetCoreInfoByParam success", t, func() {
		patch := gomonkey.ApplyFunc(config.GetCoreInfoByParam, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{Disable: true}, nil
		})
		err := InitRunLogByParam("", "")
		So(err, ShouldBeNil)
		patch.Reset()
	})
	Convey("test InitRunLogByParam", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(config.GetCoreInfoByParam, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{Disable: false}, nil
		})
		patches.ApplyFunc(newFormatLogger, func(string, config.CoreInfo) (FormatLogger, error) {
			return nil, nil
		})
		err := InitRunLogByParam("", "")
		So(err, ShouldBeNil)
		patches.Reset()
	})
}

func TestInitRunLog(t *testing.T) {
	Convey("test InitRunLogByParam GetCoreInfo fail", t, func() {
		patch := gomonkey.ApplyFunc(config.GetCoreInfo, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{}, errors.New("GetCoreInfo fail")
		})
		err := InitRunLog("", "")
		So(err, ShouldNotBeNil)
		patch.Reset()
	})
	Convey("test InitRunLogByParam GetCoreInfo success", t, func() {
		patch := gomonkey.ApplyFunc(config.GetCoreInfo, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{Disable: true}, nil
		})
		err := InitRunLog("", "")
		So(err, ShouldBeNil)
		patch.Reset()
	})
	Convey("test InitRunLog", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(config.GetCoreInfo, func(string) (config.CoreInfo, error) {
			return config.CoreInfo{Disable: false}, nil
		})
		patches.ApplyFunc(newFormatLogger, func(string, config.CoreInfo) (FormatLogger, error) {
			return nil, nil
		})
		err := InitRunLog("", "")
		So(err, ShouldBeNil)
		patches.Reset()
	})
}

func TestInitRunLogWithConfig(t *testing.T) {
	Convey("test InitRunLogWithConfig ValidateStruct fail", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(govalidator.ValidateStruct, func(interface{}) (bool, error) {
			return false, errors.New("ValidateStruct fail")
		})
		_, err := InitRunLogWithConfig("", config.CoreInfo{})
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
	Convey("test InitRunLogWithConfig ValidateStruct success", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(govalidator.ValidateStruct, func(interface{}) (bool, error) {
			return false, nil
		})
		patches.ApplyFunc(newFormatLogger, func(string, config.CoreInfo) (FormatLogger, error) {
			return nil, nil
		})
		_, err := InitRunLogWithConfig("", config.CoreInfo{})
		So(err, ShouldBeNil)
		patches.Reset()
	})
}

func TestNewFormatLogger(t *testing.T) {
	Convey("test NewFormatLogger NewWithLevel success", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(zap.NewWithLevel, func(config.CoreInfo) (*uberZap.Logger, error) {
			return &uberZap.Logger{}, nil
		})
		_, err := newFormatLogger("", config.CoreInfo{FilePath: "/test"})
		So(err, ShouldBeNil)
		patches.Reset()
	})
	Convey("test NewFormatLogger NewWithLevel fail", t, func() {
		patches := gomonkey.NewPatches()
		patches.ApplyFunc(zap.NewWithLevel, func(config.CoreInfo) (*uberZap.Logger, error) {
			return &uberZap.Logger{}, errors.New("NewWithLevel fail")
		})
		_, err := newFormatLogger("", config.CoreInfo{FilePath: "/test"})
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
}
