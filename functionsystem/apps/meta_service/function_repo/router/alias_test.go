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
	"testing"

	"meta_service/function_repo/errmsg"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"

	"meta_service/common/snerror"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
)

func Test_createFunctionAlias(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.CreateAlias,
		func(ctx server.Context, fName string, req model.AliasRequest) (
			resp model.AliasCreateResponse, err error) {
			return model.AliasCreateResponse{}, nil
		})
	defer patches.Reset()

	for _, test := range []struct {
		name         string
		method       string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test upload function code",
			"POST",
			"/function-repository/v1/functions/0-test-aa/aliases",
			`
				{"Name":"0-test-aa",
				"FunctionVersion":"1",
				"Description":"",
				"RoutingConfig":{"1":20,"2":80}}
				`,
			http.StatusOK,
			"",
		},
		{
			"test upload function code",
			"POST",
			"/function-repository/v1/functions/0-test-aa/aliases",
			``,
			errmsg.InvalidJSONBody,
			"",
		},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, test.method, test.path, body)
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

func Test_FunctionAlias(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.UpdateAlias,
		func(ctx server.Context, fName, aName string, req model.AliasUpdateRequest) (
			resp model.AliasUpdateResponse, err error) {
			return model.AliasUpdateResponse{}, nil
		})
	patches.ApplyFunc(service.DeleteAlias,
		func(ctx server.Context, req model.AliasDeleteRequest) (err error) {
			return nil
		})
	patches.ApplyFunc(service.GetAlias,
		func(ctx server.Context, req model.AliasQueryRequest) (resp model.AliasQueryResponse, err error) {
			return model.AliasQueryResponse{}, nil
		})
	patches.ApplyFunc(service.GetAliaseList,
		func(ctx server.Context, req model.AliasListQueryRequest) (
			resp model.AliasListQueryResponse, err error) {
			return model.AliasListQueryResponse{}, nil
		})
	defer patches.Reset()

	for _, test := range []struct {
		name         string
		method       string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test upload function code",
			"PUT",
			"/function-repository/v1/functions/0-test-aa/aliases/bb",
			`
				{"RevisionID":"aa",
				"FunctionVersion":"1",
				"Description":"",
				"RoutingConfig":{"1":20,"2":80}}
				`,
			http.StatusOK,
			"",
		},
		{
			"test upload function code",
			"PUT",
			"/function-repository/v1/functions/0-test-aa/aliases/bb",
			``,
			errmsg.InvalidJSONBody,
			"",
		},
		{
			"test upload function code",
			"DELETE",
			"/function-repository/v1/functions/0-test-aa/aliases/bb",
			``,
			http.StatusOK,
			"",
		},
		{
			"test upload function code",
			"GET",
			"/function-repository/v1/functions/0-test-aa/aliases",
			``,
			http.StatusOK,
			"",
		},
		{
			"test upload function code",
			"GET",
			"/function-repository/v1/functions/0-test-aa/aliases/bb",
			``,
			http.StatusOK,
			"",
		},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, test.method, test.path, body)
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
