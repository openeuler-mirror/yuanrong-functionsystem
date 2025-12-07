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

package router

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"reflect"
	"testing"
	"time"

	"github.com/agiledragon/gomonkey"
	"github.com/gin-gonic/gin"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"meta_service/common/constants"
	"meta_service/common/snerror"
	"meta_service/function_repo/model"
	"meta_service/function_repo/pkgstore"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils"
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

func Test_createLayer(t *testing.T) {
	patch := gomonkey.ApplyMethod(reflect.TypeOf(&http.Request{}), "ParseMultipartForm",
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
	defer patch.Reset()
	for _, test := range []struct {
		name         string
		path         string
		content      string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test create success", "/function-repository/v1/layers/test-layer/versions",
			"application/vnd.yuanrong+attachment;file-size=0", ``, http.StatusOK, ""},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		req := createRequest(t, "POST", test.path, body)
		req.Header.Set("Content-Type", test.content)
		req.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
		req.Header.Set(constants.HeaderDescription, "description")
		req.Header.Set(constants.HeaderLicenseInfo, "license")
		req.Header.Set(constants.HeaderDataContentType, "mock-data")
		req.MultipartForm = &multipart.Form{File: map[string][]*multipart.FileHeader{
			"file": []*multipart.FileHeader{{}},
		}}
		rec := httptest.NewRecorder()
		rec.Body = new(bytes.Buffer)
		engine.ServeHTTP(rec, req)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}

func Test_createLayer_and_updateLayer(t *testing.T) {
	Convey("Test createLayer and updateLayer with err", t, func() {
		w := httptest.NewRecorder()
		ginCtx, _ := gin.CreateTestContext(w)
		Convey("with invalid layerName length err", func() {
			patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
				*fakecontext.Context) *gin.Context {
				return ginCtx
			})
			defer patch.Reset()
			c := fakecontext.NewMockContext()
			c.Gin().Request = &http.Request{Header: make(map[string][]string)}
			c.Gin().Request.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
			c.Gin().Request.Header.Set(constants.HeaderDescription, "description")
			c.Gin().Request.Header.Set(constants.HeaderLicenseInfo, "license")
			createLayer(c, "")
			updateLayer(c, "")
		})
		Convey("with service.ParseLayerInfo err", func() {
			patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
				*fakecontext.Context) *gin.Context {
				return ginCtx
			}).ApplyFunc(service.ParseLayerInfo, func(ctx server.Context, s string) (model.LayerQueryInfo, error) {
				return model.LayerQueryInfo{}, errors.New("mock err")
			})
			defer patch.Reset()
			c := fakecontext.NewMockContext()
			c.Gin().Request = &http.Request{Header: make(map[string][]string)}
			c.Gin().Request.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
			c.Gin().Request.Header.Set(constants.HeaderDescription, "description")
			c.Gin().Request.Header.Set(constants.HeaderLicenseInfo, "license")
			createLayer(c, "aa")
			updateLayer(c, "aa")
		})
		Convey("with shouldBindJSON err", func() {
			patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
				*fakecontext.Context) *gin.Context {
				return ginCtx
			}).ApplyFunc(service.ParseLayerInfo, func(ctx server.Context, s string) (model.LayerQueryInfo, error) {
				return model.LayerQueryInfo{}, nil
			}).ApplyFunc(shouldBindJSON, func(c server.Context, v interface{}) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			c := fakecontext.NewMockContext()
			c.Gin().Request = &http.Request{Header: make(map[string][]string)}
			c.Gin().Request.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
			c.Gin().Request.Header.Set(constants.HeaderDescription, "description")
			c.Gin().Request.Header.Set(constants.HeaderLicenseInfo, "license")
			createLayer(c, "aa")
			updateLayer(c, "aa")
		})
	})
}

func Test_getLayerVersionList(t *testing.T) {
	Convey("Test getLayerVersionList", t, func() {
		w := httptest.NewRecorder()
		ginCtx, _ := gin.CreateTestContext(w)
		patchGin := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
			*fakecontext.Context) *gin.Context {
			return ginCtx
		})
		defer patchGin.Reset()
		ginCtx.Params = []gin.Param{
			gin.Param{Key: "layerName", Value: ""},
		}
		Convey("with invalid layerName length err", func() {
			c := fakecontext.NewMockContext()
			getLayerVersionList(c)
		})
		Convey("with invalid compatibleRuntime length err", func() {
			ginCtx.Params[0].Value = "aa"
			ginCtx.Request = &http.Request{URL: &url.URL{}}
			ginCtx.Request.URL.RawQuery = "compatibleRuntime=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa66"
			c := fakecontext.NewMockContext()
			getLayerVersionList(c)
		})
		Convey("with queryPaging err", func() {
			patch := gomonkey.ApplyFunc(utils.QueryPaging, func(c server.Context) (pageIndex, pageSize int, err error) {
				return 0, 0, errors.New("mock err")
			})
			defer patch.Reset()
			ginCtx.Params[0].Value = "aa"
			ginCtx.Request = &http.Request{URL: &url.URL{}}
			ginCtx.Request.URL.RawQuery = "compatibleRuntime=go"
			c := fakecontext.NewMockContext()
			getLayerVersionList(c)
		})
		Convey("with service.ParseLayerInfo err", func() {
			patch := gomonkey.ApplyFunc(utils.QueryPaging, func(c server.Context) (pageIndex, pageSize int, err error) {
				return 0, 0, nil
			}).ApplyFunc(service.ParseLayerInfo, func(ctx server.Context, s string) (model.LayerQueryInfo, error) {
				return model.LayerQueryInfo{}, errors.New("mock err")
			})
			defer patch.Reset()
			ginCtx.Params[0].Value = "aa"
			ginCtx.Request = &http.Request{URL: &url.URL{}}
			ginCtx.Request.URL.RawQuery = "compatibleRuntime=go"
			c := fakecontext.NewMockContext()
			getLayerVersionList(c)
		})

	})
}

func Test_deleteLayer(t *testing.T) {
	Test_createLayer(t)

	type args struct {
		c server.Context
	}
	for _, test := range []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test delete success", "/function-repository/v1/layers/test-layer", "", http.StatusOK, ""},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "DELETE", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}

func Test_deleteLayerVersion(t *testing.T) {
	Test_createLayer(t)

	type args struct {
		c server.Context
	}
	for _, test := range []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test delete version success",
			"/function-repository/v1/layers/test-layer/versions/1", "", http.StatusOK, ""},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "DELETE", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}

func Test_deleteLayerVersion2(t *testing.T) {
	Convey("Test deleteLayerVersion 2 with done", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyFunc(getLayerNameAndVersion, func(c server.Context) (string, int, bool) {
			return "", 0, true
		})
		defer patch.Reset()
		deleteLayerVersion(ctx)
	})
	Convey("Test deleteLayerVersion 2 with version out of range", t, func() {
		ctx := fakecontext.NewMockContext()
		w := httptest.NewRecorder()
		ginCtx, _ := gin.CreateTestContext(w)
		patch := gomonkey.ApplyFunc(getLayerNameAndVersion, func(c server.Context) (string, int, bool) {
			return "", 0, false
		}).ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
			*fakecontext.Context) *gin.Context {
			return ginCtx
		})
		defer patch.Reset()
		deleteLayerVersion(ctx)
	})
}

func Test_getLayerVersion(t *testing.T) {
	Convey("Test getLayerVersion with done", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyFunc(getLayerNameAndVersion, func(c server.Context) (string, int, bool) {
			return "", 0, true
		})
		defer patch.Reset()
		getLayerVersion(ctx)
	})
	Convey("Test getLayerVersion with version out of range", t, func() {
		ctx := fakecontext.NewMockContext()
		w := httptest.NewRecorder()
		ginCtx, _ := gin.CreateTestContext(w)
		patch := gomonkey.ApplyFunc(getLayerNameAndVersion, func(c server.Context) (string, int, bool) {
			return "", -1, false
		}).ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
			*fakecontext.Context) *gin.Context {
			return ginCtx
		})
		defer patch.Reset()
		getLayerVersion(ctx)
	})
}

func Test_getLayerNameAndVersion(t *testing.T) {
	Convey("Test getLayerNameAndVersion", t, func() {
		ctx := fakecontext.NewMockContext()
		w := httptest.NewRecorder()
		ginCtx, _ := gin.CreateTestContext(w)
		mockDone := true
		patch := gomonkey.ApplyFunc(getLayerQueryInfo, func(c server.Context) (model.LayerQueryInfo, bool) {
			return model.LayerQueryInfo{}, mockDone
		}).ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
			*fakecontext.Context) *gin.Context {
			return ginCtx
		})
		defer patch.Reset()
		Convey("when done", func() {
			getLayerNameAndVersion(ctx)
		})
		Convey("when version atoi err", func() {
			ginCtx.Params = []gin.Param{
				gin.Param{Key: "versionNumber", Value: "abc"},
			}
			mockDone = false
			getLayerNameAndVersion(ctx)
		})
	})
}

func Test_getLayer(t *testing.T) {
	Test_createLayer(t)

	type args struct {
		c server.Context
	}
	for _, test := range []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test get success", "/function-repository/v1/layers?ownerFlag=1", "", http.StatusOK, ""},
		{"test get version success", "/function-repository/v1/layers/test-layer/versions/1", "", http.StatusOK, ""},
		{"test get version list success",
			"/function-repository/v1/layers/test-layer/versions", "", http.StatusOK, ""},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "GET", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}

func Test_updateLayer(t *testing.T) {
	patch := gomonkey.ApplyMethod(reflect.TypeOf(&http.Request{}), "ParseMultipartForm",
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
	defer patch.Reset()

	body := bytes.NewBuffer([]byte(``))
	engine := RegHandlers()
	req := createRequest(t, "POST", "/function-repository/v1/layers/test-update-layer/versions", body)
	req.Header.Set("Content-Type", "application/vnd.yuanrong+attachment;file-size=0")
	req.Header.Set(constants.HeaderCompatibleRuntimes, "[\"cpp11\"]")
	req.Header.Set(constants.HeaderDescription, "description")
	req.Header.Set(constants.HeaderLicenseInfo, "license")
	req.Header.Set(constants.HeaderDataContentType, "mock-data")
	req.MultipartForm = &multipart.Form{File: map[string][]*multipart.FileHeader{
		"file": []*multipart.FileHeader{{}},
	}}
	rec := httptest.NewRecorder()
	rec.Body = new(bytes.Buffer)
	engine.ServeHTTP(rec, req)
	require.Equal(t, http.StatusOK, rec.Code)

	var resp struct {
		Result model.Layer `json:"result"`
	}
	err := json.Unmarshal(rec.Body.Bytes(), &resp)
	require.NoError(t, err)

	type args struct {
		c server.Context
	}
	for _, test := range []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test update success", "/function-repository/v1/layers/update/test-update-layer", `{
	"description": "new description",
	"licenseInfo": "MIT",
	"lastUpdateTime": "` + resp.Result.UpdateTime.Format(time.RFC3339Nano) + `",
	"version": 1
}`, http.StatusOK, ""},
	} {
		fmt.Println(test.body)
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "POST", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}

func Test_getLayerQueryInfo(t *testing.T) {
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	ctx := fakecontext.NewMockContext()
	patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return c
	}).ApplyFunc(service.ParseLayerInfo, func(ctx server.Context, s string) (model.LayerQueryInfo, error) {
		return model.LayerQueryInfo{}, errors.New("mock err")
	})
	defer patch.Reset()

	Convey("Test getLayerQueryInfo", t, func() {
		c.Params = []gin.Param{
			gin.Param{Key: "layerName", Value: ""},
		}
		_, fail := getLayerQueryInfo(ctx)
		So(fail, ShouldEqual, true)
	})
	Convey("Test getLayerQueryInfo 2", t, func() {
		c.Params = []gin.Param{
			gin.Param{Key: "layerName", Value: "abc"},
		}
		_, fail := getLayerQueryInfo(ctx)
		So(fail, ShouldEqual, true)
	})
}
