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

package service

import (
	"archive/zip"
	"bytes"
	"errors"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"reflect"
	"testing"
	"time"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"meta_service/common/constants"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/pkgstore"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/test/fakecontext"
)

type mockPackage struct{}

func (m *mockPackage) Name() string { return "" }

func (m *mockPackage) FileName() string { return "" }

func (m *mockPackage) Move() string { return "" }

func (m *mockPackage) Signature() string { return "" }

func (m *mockPackage) Size() int64 { return 64 }

func (m *mockPackage) Close() error { return nil }

type zipFile struct {
	Name, Body string
}

func newZipFile(files []zipFile) ([]byte, error) {
	buf := new(bytes.Buffer)

	w := zip.NewWriter(buf)
	for _, file := range files {
		f, err := w.Create(file.Name)
		if err != nil {
			return nil, err
		}

		_, err = f.Write([]byte(file.Body))
		if err != nil {
			return nil, err
		}
	}

	err := w.Close()
	if err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func TestLayer(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyMethod(reflect.TypeOf(&http.Request{}), "ParseMultipartForm",
		func(r *http.Request, maxMemory int64) error {
			return nil
		}).ApplyMethod(reflect.TypeOf(&multipart.FileHeader{}), "Open",
		func(*multipart.FileHeader) (multipart.File, error) {
			return &os.File{}, errors.New("mock err")
		}).ApplyMethod(reflect.TypeOf(&multipart.FileHeader{}), "Open",
		func(*multipart.FileHeader) (multipart.File, error) {
			return &os.File{}, nil
		}).ApplyFunc(pkgstore.NewPackage, func(c server.Context, name string, reader io.Reader,
		n int64) (pkgstore.Package, error) {
		return &mockPackage{}, nil
	})
	defer patches.Reset()
	ctx := fakecontext.NewContext()
	ctx.Gin().Request = &http.Request{Header: make(map[string][]string)}
	ctx.Gin().Request.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
	ctx.Gin().Request.Header.Set(constants.HeaderDescription, "description")
	ctx.Gin().Request.Header.Set(constants.HeaderLicenseInfo, "license")
	ctx.Gin().Request.MultipartForm = &multipart.Form{File: map[string][]*multipart.FileHeader{
		"file": []*multipart.FileHeader{{}},
	}}
	ctx.InitTenantInfo(server.TenantInfo{
		BusinessID: "yrk",
		TenantID:   "qqq",
		ProductID:  "",
	})

	req := model.CreateLayerRequest{
		ZipFileSize:        100,
		CompatibleRuntimes: "[\"cpp11\"]",
		Description:        "this is my layer",
		LicenseInfo:        "MIT",
	}
	_, err := CreateLayer(ctx, "my-layer", req)
	require.NoError(t, err, "create layer")

	_, err = CreateLayer(ctx, "my-layer", req)
	require.NoError(t, err, "create layer")

	_, err = CreateLayer(ctx, "my-layer-2", req)
	require.NoError(t, err, "create layer")

	err = DeleteLayerVersion(ctx, "my-layer", 1)
	require.NoError(t, err, "delete layer")

	err = DeleteLayerVersion(ctx, "my-layer-2", 1)
	require.NoError(t, err, "delete layer")

	_, err = CreateLayer(ctx, "my-layer", req)
	require.NoError(t, err, "create layer")

	layer, err := GetLayerVersion(ctx, "my-layer", 0)
	require.NoError(t, err, "get layer")
	assert.Equal(t, "my-layer", layer.Name)
	assert.Equal(t, 3, layer.Version)
	assert.Equal(t, "this is my layer", layer.Description)
	assert.Equal(t, "MIT", layer.LicenseInfo)

	layer, err = GetLayerVersion(ctx, "my-layer", 2)
	require.NoError(t, err, "get layer")
	assert.Equal(t, "my-layer", layer.Name)
	assert.Equal(t, 2, layer.Version)
	assert.Equal(t, "this is my layer", layer.Description)
	assert.Equal(t, "MIT", layer.LicenseInfo)

	_, err = UpdateLayer(ctx, "my-layer", model.UpdateLayerRequest{
		Description:    "new description",
		LicenseInfo:    "Apache",
		LastUpdateTime: layer.UpdateTime,
		Version:        2,
	})
	require.NoError(t, err, "update layer")

	list, err := GetLayerVersionList(ctx, model.GetLayerVersionListRequest{
		LayerName:         "my-layer",
		CompatibleRuntime: "",
		PageIndex:         0,
		PageSize:          0,
	})
	require.NoError(t, err, "get layer list")
	assert.Equal(t, 2, list.Total)
	assert.Equal(t, 2, len(list.LayerVersions))

	listreq := model.GetLayerListRequest{
		LayerName:         "layer",
		LayerVersion:      0,
		CompatibleRuntime: "",
		OwnerFlag:         1,
		PageIndex:         0,
		PageSize:          0,
	}
	list, err = GetLayerList(ctx, listreq)
	require.NoError(t, err, "get layer list")
	assert.Equal(t, 2, list.Total)
	assert.Equal(t, 2, len(list.LayerVersions))
}

func TestParseLayerInfo(t *testing.T) {
	Convey("Test ParseLayerInfo", t, func() {
		ctx := fakecontext.NewMockContext()
		// sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:layer:aa:1
		_, err := ParseLayerInfo(ctx, "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:layer:aa:1")
		So(err, ShouldBeNil)
		_, err = ParseLayerInfo(ctx, "sn:cn:yrk123:i1fe539427b24702acc11fbb4e134e17:layer:aa:1")
		So(err, ShouldNotBeNil)
	})
}

func TestCreateLayerHelper(t *testing.T) {
	Convey("Test createLayerHelper", t, func() {
		txn := &storage.Txn{}
		Convey("when CreateLayer err", func() {
			patches := gomonkey.ApplyFunc(storage.CreateLayer,
				func(*storage.Txn, string, int, storage.LayerValue) error {
					return errors.New("mock err")
				})
			defer patches.Reset()
			_, err := createLayerHelper(txn, "a", 1, storage.LayerValue{})
			So(err, ShouldNotBeNil)
		})
		Convey("when Commit err", func() {
			patches := gomonkey.ApplyFunc(storage.CreateLayer,
				func(*storage.Txn, string, int, storage.LayerValue) error {
					return nil
				}).ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
				return errors.New("mock err")
			})
			defer patches.Reset()
			_, err := createLayerHelper(txn, "a", 1, storage.LayerValue{})
			So(err, ShouldNotBeNil)
		})
		Convey("when buildLayer err", func() {
			patches := gomonkey.ApplyFunc(storage.CreateLayer, func(
				*storage.Txn, string, int, storage.LayerValue) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
				return nil
			}).ApplyFunc(buildLayer, func(
				server.Context, *model.Layer, string, int, storage.LayerValue) error {
				return errors.New("mock err")
			})
			defer patches.Reset()
			_, err := createLayerHelper(txn, "a", 1, storage.LayerValue{})
			So(err, ShouldNotBeNil)
		})

	})
}

func TestGetLayerVersion(t *testing.T) {
	ctx := fakecontext.NewMockContext()
	Convey("Test GetLayerVersion when GetLayerLatestVersion err", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetLayerLatestVersion, func(
			ctx server.Context, layerName string) (storage.LayerValue, int, error) {
			return storage.LayerValue{}, 0, errmsg.KeyNotFoundError
		})
		_, err := GetLayerVersion(ctx, "mock-layer", 0)
		So(err, ShouldNotBeNil)
		patches.Reset()

		patches.ApplyFunc(storage.GetLayerLatestVersion, func(
			ctx server.Context, layerName string) (storage.LayerValue, int, error) {
			return storage.LayerValue{}, 0, errors.New("mock err")
		})
		_, err = GetLayerVersion(ctx, "mock-layer", 0)
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
	Convey("Test GetLayerVersion when buildLayer err", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetLayerLatestVersion, func(
			ctx server.Context, layerName string) (storage.LayerValue, int, error) {
			return storage.LayerValue{}, 0, nil
		}).ApplyFunc(buildLayer, func(
			ctx server.Context, mlayer *model.Layer, layerName string, layerVersion int, layer storage.LayerValue,
		) error {
			return errors.New("mock err")
		})
		_, err := GetLayerVersion(ctx, "mock-layer", 0)
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
	Convey("Test GetLayerVersion when buildLayer err", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetLayerVersion, func(
			server.Context, string, int) (storage.LayerValue, error) {
			return storage.LayerValue{}, errmsg.KeyNotFoundError
		})
		_, err := GetLayerVersion(ctx, "mock-layer", 1)
		So(err, ShouldNotBeNil)
		patches.Reset()

		patches.ApplyFunc(storage.GetLayerVersion, func(
			server.Context, string, int) (storage.LayerValue, error) {
			return storage.LayerValue{}, errors.New("mock err")
		})
		_, err = GetLayerVersion(ctx, "mock-layer", 1)
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
	Convey("Test GetLayerVersion when buildLayer err 2", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetLayerVersion, func(
			server.Context, string, int) (storage.LayerValue, error) {
			return storage.LayerValue{}, nil
		}).ApplyFunc(buildLayer, func(
			ctx server.Context, mlayer *model.Layer, layerName string, layerVersion int, layer storage.LayerValue,
		) error {
			return errors.New("mock err")
		})
		_, err := GetLayerVersion(ctx, "mock-layer", 1)
		So(err, ShouldNotBeNil)
		patches.Reset()
	})
}

func TestGetLayerVersionList(t *testing.T) {
	Convey("Test GetLayerVersionList", t, func() {
		mode := 0
		patches := gomonkey.ApplyFunc(isCompatibleRuntimeValid, func(cps []string) error {
			if mode == 1 {
				return errors.New("mock err")
			}
			return nil
		}).ApplyFunc(storage.GetLayerStream, func(ctx server.Context, layerName string) (*storage.LayerPrepareStmt, error) {
			if mode == 2 {
				return nil, errors.New("mock err")
			}
			return nil, nil
		}).ApplyFunc(getLayerListHelper, func(
			ctx server.Context, stream *storage.LayerPrepareStmt, compatibleRuntime string, pageIndex, pageSize int,
		) (model.LayerList, error) {
			if mode == 3 {
				return model.LayerList{}, errmsg.KeyNotFoundError
			}
			return model.LayerList{}, errors.New("mock err")
		})
		defer patches.Reset()
		for mode = 0; mode < 4; mode++ {
			_, err := GetLayerVersionList(fakecontext.NewMockContext(), model.GetLayerVersionListRequest{
				CompatibleRuntime: "custom-mock",
			})
			So(err, ShouldNotBeNil)
		}
	})
}

func TestUpdateLayer(t *testing.T) {
	Convey("Test UpdateLayer", t, func() {
		mockLastUpdateTime := time.Now()
		time2 := mockLastUpdateTime.Add(time.Second * 10)
		txn := &storage.Txn{}
		patches := gomonkey.NewPatches()
		defer patches.Reset()
		patches.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
			return txn
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
		mode := 0
		patches.ApplyFunc(storage.GetLayerVersionTx, func(txn storage.Transaction, layerName string, layerVersion int) (storage.LayerValue, error) {
			if mode == 0 {
				return storage.LayerValue{}, errors.New("mock err")
			}
			return storage.LayerValue{UpdateTime: mockLastUpdateTime}, nil
		})
		patches.ApplyFunc(storage.UpdateLayer, func(txn *storage.Txn, layerName string, layerVersion int, layer storage.LayerValue) error {
			if mode == 2 {
				return errors.New("mock err")
			}
			return nil
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
			if mode == 3 {
				return errors.New("mock err")
			}
			return nil
		})
		patches.ApplyFunc(buildLayer, func(
			ctx server.Context, mlayer *model.Layer, layerName string, layerVersion int, layer storage.LayerValue,
		) error {
			if mode == 4 {
				return errors.New("mock err")
			}
			return nil
		})
		for mode = 0; mode < 5; mode++ {
			req := model.UpdateLayerRequest{}
			if mode == 1 {
				req.LastUpdateTime = time2
			} else {
				req.LastUpdateTime = mockLastUpdateTime
			}
			_, err := UpdateLayer(fakecontext.NewMockContext(), "mock-layer", req)
			So(err, ShouldNotBeNil)
		}
	})
}
