/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
	"errors"
	"net/http"
	"testing"

	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
)

func Test_publishFunction(t *testing.T) {
	engine := RegHandlers()
	// POST /serverless/v1/functions/:functionName/versions
	Convey("Test publishFunction 1", t, func() {
		body := bytes.NewBuffer([]byte(""))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions//versions", body)
		So(rec.Code, ShouldEqual, http.StatusInternalServerError)
	})
	Convey("Test publishFunction 2", t, func() {
		body := bytes.NewBuffer([]byte(""))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/abc123/versions", body)
		So(rec.Code, ShouldEqual, http.StatusInternalServerError)
	})
	Convey("Test publishFunction 3 with ParseFunctionInfo err", t, func() {
		patch := gomonkey.ApplyFunc(service.ParseFunctionInfo, func(ctx server.Context,
			queryInfo, qualifier string,
		) (model.FunctionQueryInfo, error) {
			return model.FunctionQueryInfo{}, errors.New("mock err")
		})
		defer patch.Reset()
		body := bytes.NewBuffer([]byte(`{"revisionId":"mock-rev-id","description":"mock-desc"}`))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/abc123/versions", body)
		So(rec.Code, ShouldEqual, http.StatusInternalServerError)
	})
	patches := gomonkey.ApplyFunc(service.ParseFunctionInfo, func(ctx server.Context,
		queryInfo, qualifier string,
	) (model.FunctionQueryInfo, error) {
		return model.FunctionQueryInfo{}, nil
	})
	defer patches.Reset()
	Convey("Test publishFunction 4 with CheckFunctionVersion err", t, func() {
		patch := gomonkey.ApplyFunc(service.CheckFunctionVersion, func(ctx server.Context, functionName string) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		body := bytes.NewBuffer([]byte(`{"revisionId":"mock-rev-id","description":"mock-desc"}`))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/abc123/versions", body)
		So(rec.Code, ShouldEqual, http.StatusInternalServerError)
	})
	Convey("Test publishFunction 5 with PublishFunction err", t, func() {
		patch := gomonkey.ApplyFunc(service.CheckFunctionVersion, func(ctx server.Context, functionName string) error {
			return nil
		}).ApplyFunc(service.PublishFunction, func(
			ctx server.Context, funcName string, req model.PublishRequest,
		) (model.PublishResponse, error) {
			return model.PublishResponse{}, errors.New("mock err")
		})
		defer patch.Reset()
		body := bytes.NewBuffer([]byte(`{"revisionId":"mock-rev-id","description":"mock-desc"}`))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/abc123/versions", body)
		So(rec.Code, ShouldEqual, http.StatusInternalServerError)
	})
	Convey("Test publishFunction 6", t, func() {
		patch := gomonkey.ApplyFunc(service.CheckFunctionVersion, func(ctx server.Context, functionName string) error {
			return nil
		}).ApplyFunc(service.PublishFunction, func(
			ctx server.Context, funcName string, req model.PublishRequest,
		) (model.PublishResponse, error) {
			return model.PublishResponse{}, nil
		})
		defer patch.Reset()
		body := bytes.NewBuffer([]byte(`{"revisionId":"mock-rev-id","description":"mock-desc"}`))
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/abc123/versions", body)
		So(rec.Code, ShouldEqual, http.StatusOK)
	})
}
