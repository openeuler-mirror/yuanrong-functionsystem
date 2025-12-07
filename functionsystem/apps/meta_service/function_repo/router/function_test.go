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
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"

	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
)

func Test_uploadFunctionCode(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.GetUploadFunctionCodeResponse,
		func(ctx server.Context, info model.FunctionQueryInfo,
			req model.FunctionCodeUploadRequest) (model.FunctionVersion, error) {
			return model.FunctionVersion{}, nil
		})
	defer patches.Reset()

	for _, test := range []struct {
		name         string
		method       string
		path         string
		content      string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test upload function code", "PUT", "/function-repository/v1/functions/0-test-aa/code", "application/vnd.yuanrong+attachment;file-size=0;revision-id=0", ``, http.StatusOK, ""},
		{"test upload function code", "PUT", "/function-repository/v1/functions/0-test-aa/code", "", ``, errmsg.InvalidUserParam, ""},
		{"test upload function code", "PUT", "/function-repository/v1/functions/0-test-aa/code", "application/vnd.yuanrong+attachment;file-size=a", ``, errmsg.InvalidUserParam, ""},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		req := createRequest(t, test.method, test.path, body)
		req.Header.Set("Content-Type", test.content)
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
