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

package utils

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	. "github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/suite"
)

type IsFileTestSuite struct {
	suite.Suite
	tempDir string
}

// SetupSuite Setup Suite
func (suite *IsFileTestSuite) SetupSuite() {
	var err error

	// Create temp dir for IsFileTestSuite
	suite.tempDir, err = ioutil.TempDir("", "isfile-test")
	suite.Require().NoError(err)
}

// TearDownSuite TearDown Suite
func (suite *IsDirTestSuite) TearDownSuite() {
	defer os.RemoveAll(suite.tempDir)
}

// TestPositive Test Positive
func (suite *IsFileTestSuite) TestPositive() {

	// Create temp file
	tempFile, err := ioutil.TempFile(suite.tempDir, "temp_file")
	suite.Require().NoError(err)
	defer os.Remove(tempFile.Name())

	// Verify that function isFile() returns true when file is created
	suite.Require().True(IsFile(tempFile.Name()))

}

// TestFileIsNotExist Test File Is Not Exist
func (suite *IsFileTestSuite) TestFileIsNotExist() {

	// Set path to unexisted file
	tempFile := filepath.Join(suite.tempDir, "somePath.txt")

	// Verify that function isFile() returns false when file doesn't exist in the system
	suite.Require().False(IsFile(tempFile))
}

// TestFileIsADirectory Test File Is A Directory
func (suite *IsFileTestSuite) TestFileIsADirectory() {
	suite.Require().False(IsFile(suite.tempDir))
}

type IsDirTestSuite struct {
	suite.Suite
	tempDir string
}

// SetupSuite Setup Suite
func (suite *IsDirTestSuite) SetupSuite() {
	var err error

	// Create temp dir for IsDirTestSuite
	suite.tempDir, err = ioutil.TempDir("", "isdir-test")
	suite.Require().NoError(err)
}

// TearDownSuite TearDown Suite
func (suite *IsFileTestSuite) TearDownSuite() {
	defer os.RemoveAll(suite.tempDir)
}

// TestPositive Test Positive
func (suite *IsDirTestSuite) TestPositive() {

	// Verify that function IsDir() returns true when directory exists in the system
	suite.Require().True(IsDir(suite.tempDir))
}

// TestNegative Test Negative
func (suite *IsDirTestSuite) TestNegative() {

	// Create temp file
	tempFile, err := ioutil.TempFile(suite.tempDir, "temp_file")
	suite.Require().NoError(err)
	defer os.Remove(tempFile.Name())

	// Verify that function IsDir( returns false when file instead of directory is function argument
	suite.Require().False(IsDir(tempFile.Name()))
}

type FileExistTestSuite struct {
	suite.Suite
	tempDir string
}

// SetupSuite Setup Suite
func (suite *FileExistTestSuite) SetupSuite() {
	var err error

	// Create temp dir for FileExistTestSuite
	suite.tempDir, err = ioutil.TempDir("", "file_exists-test")
	suite.Require().NoError(err)
}

// TearDownSuite TearDown Suite
func (suite *FileExistTestSuite) TearDownSuite() {
	defer os.RemoveAll(suite.tempDir)
}

// TestPositive Test Positive
func (suite *FileExistTestSuite) TestPositive() {

	// Create temp file
	tempFile, err := ioutil.TempFile(suite.tempDir, "temp_file")
	suite.Require().NoError(err)
	defer os.Remove(tempFile.Name())

	// Verify that function FileExists() returns true when file is exist
	suite.Require().True(FileExists(tempFile.Name()))
}

// TestFileNotExist Test File Not Exist
func (suite *FileExistTestSuite) TestFileNotExist() {

	// Set path to unexisted file
	tempFile := filepath.Join(suite.tempDir, "somePath.txt")

	// Verify that function FileExists() returns false when file doesn't exist
	suite.Require().False(FileExists(tempFile))
}

// TestFileIsNotAFile Test File Is Not A File
func (suite *FileExistTestSuite) TestFileIsNotAFile() {

	// Verify that function returns true when folder is exist in the system
	suite.Require().True(FileExists(suite.tempDir))
}

// TestHelperTestSuite Test Helper Test Suite
func TestHelperTestSuite(t *testing.T) {
	suite.Run(t, new(FileExistTestSuite))
	suite.Run(t, new(IsDirTestSuite))
	suite.Run(t, new(IsFileTestSuite))
}

// TestHelperTestSuite Test Helper Test Suite
func TestGetDefaultPath(t *testing.T) {
	Convey("test get defaultpath", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, nil
			}),
		}
		path, res := GetDefaultPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, "/home/sn")
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test get defaultpath 1 ", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return "/opt", nil
			}),
		}
		path, res := GetDefaultPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, "/opt/..")
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test get defaultpath 2", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, errors.New("failed")
			}),
		}
		path, res := GetDefaultPath()
		So(res, ShouldNotEqual, nil)
		So(res.Error(), ShouldEqual, "failed")
		So(path, ShouldEqual, "")
		for i := range patches {
			patches[i].Reset()
		}
	})
}

// TestGetFunctionConfigPath -
func TestGetFunctionConfigPath(t *testing.T) {
	Convey("test GetFunctionConfigPath", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return "/opt", nil
			}),
		}
		path, res := GetFunctionConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, "/opt/../config/function.yaml")
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetFunctionConfigPath 2", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, nil
			}),
		}
		path, res := GetFunctionConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, DefaultFunctionPath)
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetFunctionConfigPath 3", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, errors.New("failed")
			}),
		}
		path, res := GetFunctionConfigPath()
		So(res, ShouldNotEqual, nil)
		So(res.Error(), ShouldEqual, "failed")
		So(path, ShouldEqual, "")
		for i := range patches {
			patches[i].Reset()
		}
	})
}

// TestGetLogConfigPath -
func TestGetLogConfigPath(t *testing.T) {
	Convey("test GetLogConfigPath", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return "/opt", nil
			}),
		}
		path, res := GetLogConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, "/opt/../config/log.json")
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetLogConfigPath 2", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, nil
			}),
		}
		path, res := GetLogConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, defaultLogConfigPath)
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetLogConfigPath 3", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, errors.New("failed")
			}),
		}
		path, res := GetLogConfigPath()
		So(res, ShouldNotEqual, nil)
		So(res.Error(), ShouldEqual, "failed")
		So(path, ShouldEqual, "")
		for i := range patches {
			patches[i].Reset()
		}
	})
}

// TestGetConfigPath -
func TestGetConfigPath(t *testing.T) {
	Convey("test GetConfigPath", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return "/opt", nil
			}),
		}
		path, res := GetConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, "/opt/../config/config.json")
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetConfigPath 2", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, nil
			}),
		}
		path, res := GetConfigPath()
		So(res, ShouldEqual, nil)
		So(path, ShouldEqual, defaultConfigPath)
		for i := range patches {
			patches[i].Reset()
		}
	})
	Convey("test GetConfigPath 3", t, func() {
		patches := [...]*Patches{
			ApplyFunc(GetBinPath, func() (string, error) {
				return defaultBinPath, errors.New("failed")
			}),
		}
		path, res := GetConfigPath()
		So(res, ShouldNotEqual, nil)
		So(res.Error(), ShouldEqual, "failed")
		So(path, ShouldEqual, "")
		for i := range patches {
			patches[i].Reset()
		}
	})
}

func TestGetBinPath(t *testing.T) {
	GetBinPath()
}

func TestGetResourcePath(t *testing.T) {
	GetResourcePath()

	os.Setenv("ResourcePath", "")
	GetResourcePath()

	patch := ApplyFunc(exec.LookPath, func(string) (string, error) {
		return "", errors.New("test")
	})
	GetResourcePath()
	patch.Reset()

	patch = ApplyFunc(filepath.Abs, func(string) (string, error) {
		return "", errors.New("test")
	})
	GetResourcePath()
	fmt.Println()
	patch.Reset()
}

func TestIsDir(t *testing.T) {
	IsDir("")
}

func TestGetServicesPath(t *testing.T) {
	GetServicesPath()
	os.Setenv("ServicesPath", "")
	GetServicesPath()

	patch := ApplyFunc(exec.LookPath, func(string) (string, error) {
		return "", errors.New("test")
	})
	GetServicesPath()
	patch.Reset()

	patch = ApplyFunc(filepath.Abs, func(string) (string, error) {
		return "", errors.New("test")
	})
	GetServicesPath()
	fmt.Println()
	patch.Reset()
}
