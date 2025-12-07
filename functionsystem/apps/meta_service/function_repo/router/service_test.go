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
	"net/http/httptest"
	"reflect"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/gin-gonic/gin"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/test/fakecontext"
)

func TestGetService(t *testing.T) {
	Convey("Test getService", t, func() {
		w := httptest.NewRecorder()
		c, _ := gin.CreateTestContext(w)

		c.Params = []gin.Param{
			gin.Param{Key: "snServiceId", Value: "12345678901234567890"},
		}
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin",
			func(*fakecontext.Context) *gin.Context {
				return c
			}).ApplyFunc(service.GetServiceID, func(ctx server.Context, id, _ string) (model.ServiceGetResponse, error) {
			return model.ServiceGetResponse{}, nil
		})
		defer patch.Reset()
		getService(ctx)

		c.Params = []gin.Param{
			gin.Param{Key: "snServiceId", Value: "123"},
		}
		getService(ctx)
	})
}
