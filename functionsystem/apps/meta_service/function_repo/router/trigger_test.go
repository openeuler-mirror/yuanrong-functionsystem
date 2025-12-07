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
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"reflect"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/gin-gonic/gin"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"

	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils"
)

func TestTrigger(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.CreateTrigger,
		func(ctx server.Context, req model.TriggerCreateRequest) (model.TriggerResponse, error) {
			return model.TriggerResponse{}, nil
		})
	patches.ApplyFunc(service.UpdateTrigger,
		func(ctx server.Context, req model.TriggerUpdateRequest) (model.TriggerResponse, error) {
			return model.TriggerResponse{}, nil
		})
	patches.ApplyFunc(service.DeleteTriggerByID,
		func(ctx server.Context, tid string) (err error) {
			return nil
		})
	patches.ApplyFunc(service.DeleteTriggerByFuncID,
		func(ctx server.Context, fid string) error {
			return nil
		})
	patches.ApplyFunc(service.GetTrigger,
		func(ctx server.Context, tid string) (model.TriggerResponse, error) {
			return model.TriggerResponse{}, nil
		})
	patches.ApplyFunc(service.GetTriggerList,
		func(ctx server.Context, pageIndex, pageSize int, fid string) (model.TriggerListGetResponse, error) {
			return model.TriggerListGetResponse{}, nil
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
		{"test trigger create", "POST", "/function-repository/v1/triggers", `{
	"functionid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
	"triggertype": "HTTP",
	"spec": {
		"funcid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
		"triggerid": "",
		"triggertype": "HTTP",
		"httpmethod": "PUT",
		"resourceid": "iamsff",
		"authflag": false,
		"authalgorithm": "",
		"triggerurl": "",
		"appid": "",
		"appsecret": ""
	}
}`, http.StatusOK, ""},
		{"test trigger create2", "POST", "/function-repository/v1/triggers",
			`{`, 121052, "request body is not a valid JSON object"},
		{"test trigger create3", "POST", "/function-repository/v1/triggers", `{
	"functionid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
	"triggertype": "HTTP",
	"spec": ""
}`, 4104, "json: cannot unmarshal string into Go value of type model.HTTPTriggerSpec. check input parameters"},
		{"test trigger create4", "POST", "/function-repository/v1/triggers", `{
	"functionid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
	"triggertype": "unkown"
}`, 4104, "invalid triggertype: unkown. check input parameters"},
		{"test trigger delete", "DELETE", "/function-repository/v1/triggers/1f05e8e5-ce0d-4c4a-903a-eae27b8f9d04",
			"",
			http.StatusOK, ""},
		{"test trigger delete", "DELETE", "/function-repository/v1/triggers/function/sn:cn:businessid:tenantid@productid:function:test:$latest",
			"",
			http.StatusOK, ""},
		{"test trigger get", "GET", "/function-repository/v1/triggers/1f05e8e5-ce0d-4c4a-903a-eae27b8f9d04",
			"",
			http.StatusOK, ""},
		{"test trigger get list", "GET", "/function-repository/v1/triggers?funcId=sn:cn:businessid:tenantid@productid:function:test:$latest",
			"",
			http.StatusOK, ""},
		{"test update trigger", "PUT", "/function-repository/v1/triggers/1f05e8e5-ce0d-4c4a-903a-eae27b8f9d04",
			"abc123", errmsg.InvalidJSONBody, "request body is not a valid JSON object"},
		{"test update trigger 2", "PUT", "/function-repository/v1/triggers/66666666666666666666666666666666666666666666666666666666666666666",
			`{
	"functionid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
	"triggertype": "HTTP",
	"spec": {
		"funcid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
		"triggerid": "",
		"triggertype": "HTTP",
		"httpmethod": "PUT",
		"resourceid": "iamsff",
		"authflag": false,
		"authalgorithm": "",
		"triggerurl": "",
		"appid": "",
		"appsecret": ""
	}
}`, errmsg.InvalidUserParam, "invalid triggerid length: 65. check input parameters"},
		{"test update trigger 3", "PUT", "/function-repository/v1/triggers/1f05e8e5-ce0d-4c4a-903a-eae27b8f9d04",
			`{
	"functionid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
	"triggertype": "HTTP",
	"spec": {
		"funcid": "sn:cn:businessid:tenantid@productid:function:test:$latest",
		"triggerid": "",
		"triggertype": "HTTP",
		"httpmethod": "PUT",
		"resourceid": "iamsff",
		"authflag": false,
		"authalgorithm": "",
		"triggerurl": "",
		"appid": "",
		"appsecret": ""
	}
}`, http.StatusOK, ""},
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

func Test_checkFunctionID(t *testing.T) {
	err := checkFunctionID("")
	assert.NotNil(t, err)
}

func Test_getTriggerList(t *testing.T) {
	w := httptest.NewRecorder()
	ginCtx, _ := gin.CreateTestContext(w)
	patchGin := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return ginCtx
	})
	defer patchGin.Reset()
	Convey("Test getTriggerList", t, func() {
		Convey("with queryPaging err", func() {
			patch := gomonkey.ApplyFunc(utils.QueryPaging, func(c server.Context) (pageIndex, pageSize int, err error) {
				return 0, 0, errors.New("mock err")
			})
			defer patch.Reset()
			c := fakecontext.NewMockContext()
			getTriggerList(c)
		})
		Convey("with checkFunctionID err", func() {
			patch := gomonkey.ApplyFunc(checkFunctionID, func(fid string) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			ginCtx.Request = &http.Request{URL: &url.URL{}}
			ginCtx.Request.URL.RawQuery = "funcId=a1"
			c := fakecontext.NewMockContext()
			getTriggerList(c)
		})
	})
}

func Test_deleteTriggers(t *testing.T) {
	w := httptest.NewRecorder()
	ginCtx, _ := gin.CreateTestContext(w)
	patchGin := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return ginCtx
	})
	defer patchGin.Reset()

	Convey("Test deleteTriggers with triggerId invalid param err", t, func() {
		ginCtx.Params = []gin.Param{
			gin.Param{Key: "triggerId", Value: "mock-fu"},
		}
		c := fakecontext.NewMockContext()
		deleteTriggers(c)
	})
	Convey("Test deleteTriggers with checkFunctionID err", t, func() {
		ginCtx.Params = []gin.Param{
			{Key: "triggerId", Value: "function"},
			{Key: "functionId", Value: ""},
		}
		c := fakecontext.NewMockContext()
		deleteTriggers(c)
	})
}

func Test_deleteTrigger(t *testing.T) {
	w := httptest.NewRecorder()
	ginCtx, _ := gin.CreateTestContext(w)
	patchGin := gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "Gin", func(
		*fakecontext.Context) *gin.Context {
		return ginCtx
	})
	defer patchGin.Reset()

	Convey("Test deleteTrigger with triggerId invalid param err", t, func() {
		ginCtx.Params = []gin.Param{
			gin.Param{Key: "triggerId", Value: "function"},
		}
		c := fakecontext.NewMockContext()
		deleteTrigger(c)
	})
	Convey("Test deleteTrigger with checkTriggerID err", t, func() {
		ginCtx.Params = []gin.Param{
			gin.Param{Key: "triggerId", Value: "function"},
		}
		c := fakecontext.NewMockContext()
		deleteTrigger(c)
	})
}
