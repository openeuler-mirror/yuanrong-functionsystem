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
	"errors"
	"net/http/httptest"
	"reflect"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/gin-gonic/gin"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils"
)

func Test_queryPaging(t *testing.T) {
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	ctx := fakecontext.NewMockContext()
	mockPageIndex := "0"
	mockPageSize := "0"
	patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return c
	}).ApplyMethod(reflect.TypeOf(c), "Query", func(c *gin.Context, key string) string {
		if key == "pageIndex" {
			return mockPageIndex
		}
		return mockPageSize
	})
	defer patch.Reset()
	Convey("Test queryPaging", t, func() {
		mockPageIndex = "0"
		_, _, err := utils.QueryPaging(ctx)
		So(err, ShouldNotBeNil)

		mockPageIndex = "xxx"
		_, _, err = utils.QueryPaging(ctx)
		So(err, ShouldNotBeNil)

		mockPageIndex = "1"

		mockPageSize = "0"
		_, _, err = utils.QueryPaging(ctx)
		So(err, ShouldNotBeNil)

		mockPageSize = "xxx"
		_, _, err = utils.QueryPaging(ctx)
		So(err, ShouldNotBeNil)

		mockPageSize = "1"
		_, _, err = utils.QueryPaging(ctx)
		So(err, ShouldBeNil)
	})

}

func Test_shouldBindQuery(t *testing.T) {
	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)
	ctx := fakecontext.NewMockContext()
	patch := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return c
	}).ApplyMethod(reflect.TypeOf(c), "ShouldBindQuery", func(c *gin.Context, obj interface{}) error {
		return errors.New("mock err")
	})
	defer patch.Reset()
	Convey("Test shouldBindQuery", t, func() {
		err := utils.ShouldBindQuery(ctx, 1)
		So(err, ShouldNotBeNil)
	})
}
