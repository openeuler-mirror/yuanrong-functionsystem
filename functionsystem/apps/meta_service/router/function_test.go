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
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"testing"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/storage"

	"meta_service/common/snerror"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"
)

func Test_createFunction(t *testing.T) {
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
		{"test create success", "/serverless/v1/functions", "{\n" +
			"\t\"name\": \"0-test-aa\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"kind\": \"yr-lib\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 500,\n" +
			"\t\"memory\": 500,\n" +
			"\t\"timeout\": 500,\n" +
			"\t\"codeUploadType\": \"\"\n" +
			"}", http.StatusOK, ""},
		{"test cpu out of range", "/serverless/v1/functions", "{\n" +
			"\t\"name\": \"0-test-cc\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"kind\": \"yr-lib\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 16001,\n" +
			"\t\"memory\": 500,\n" +
			"\t\"timeout\": 500\n" +
			"}", errmsg.InvalidUserParam, "CPU not in range [300, 16000]" +
			". check input parameters"},
		{"test cpu out of range", "/serverless/v1/functions", "{\n" +
			"\t\"name\": \"0-test-bb\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"kind\": \"yr-lib\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 801,\n" +
			"\t\"memory\": 8201,\n" +
			"\t\"timeout\": 500\n" +
			"}", errmsg.InvalidUserParam, "memory not in range [128, 8200]" +
			". check input parameters"},
		{
			"test prestop is invalid", "/serverless/v1/functions",
			"{ \"codeUploadType\": \"\",    \"name\": \"0@faaspy@hello1\",      \"handler\": \"handler.my_handler\",     \"runtime\": \"python3.9\",     \"description\": \"\",     \"cpu\": 300,     \"memory\": 128,     \"timeout\": 30,     \"concurrentNum\": \"100\",     \"minInstance\": \"1\",     \"maxInstance\": \"1\",     \"environment\": {},     \"layers\": [],     \"kind\": \"faas\",     \"codePath\": \"\",     \"storageType\": \"local\",     \"s3CodePath\": {           \"bucketId\": \"bucket-test-log1\",           \"objectId\": \"0@faaspy@hello-1719921552952\",         \"bucketUrl\": \"http://10.247.23.146:30110/\",         \"token\": \"\"     },     \"schedulePolicies\": [],     \"extendedHandler\": {          \"initializer\": \"handler.init\",          \"pre_stop\": \"test.prestop\"     },     \"extendedTimeout\": {         \"initializer\": 600,         \"pre_stop\": -1     },     \"customResources\": {} }",
			4104, "preStop timeOut must between 0 and 180. check input parameters",
		},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "POST", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			assert.Equal(t, 400, rec.Code)
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

func Test_deleteFunction(t *testing.T) {
	type args struct {
		c server.Context
	}
	tests := []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test delete failed", "/serverless/v1/functions/aa", "",
			errmsg.FunctionNotFound, "function [aa] is not found. check input parameters",
		},
		{
			"test delete success", "/serverless/v1/functions/0-test-aa", "",
			http.StatusOK, "",
		},
	}

	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		body := bytes.NewBuffer([]byte(tt.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "DELETE", tt.path, body)
		fmt.Printf("%v\n", rec)
		badResponse := &snerror.BadResponse{}
		if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
			t.Errorf("%s", err)
		}
		assert.Equal(t, tt.expectedCode, badResponse.Code)
		if tt.expectedMsg != "" {
			assert.Equal(t, tt.expectedMsg, badResponse.Message)
		}
		assert.Equal(t, 404, rec.Code)
	})

	tt = tests[1]
	t.Run(tt.name, func(t *testing.T) {
		body := bytes.NewBuffer([]byte("{\n" +
			"\t\"name\": \"0-test-aa\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 500,\n" +
			"\t\"memory\": 500,\n" +
			"\t\"timeout\": 500\n" +
			"}"))
		engine := RegHandlers()
		// create
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions", body)
		// delete
		body = bytes.NewBuffer([]byte(tt.body))
		_, rec = routerRequest(t, engine, "DELETE", tt.path, body)
		fmt.Printf("%v\n", rec)
		assert.Equal(t, tt.expectedCode, rec.Code)
	})
}

/*
func Test_updateFunction(t *testing.T) {
	type args struct {
		c server.Context
	}
	tests := []struct {
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test delete success", "/serverless/v1/functions/" +
			"sn:cn:yrk:12345678901234561234567890123456:function:0-test-aa:$latest", "{\n" +
			"\t\"name\": \"0-test-aa\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"kind\": \"yr-lib\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 500,\n" +
			"\t\"memory\": 500,\n" +
			"\t\"timeout\": 500,\n" +
			"\t\"revisionId\": \"{}\"\n" +
			"}",
			http.StatusOK, ""},
	}

	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		body := bytes.NewBuffer([]byte("{\n" +
			"\t\"name\": \"0-test-aa\",\n" +
			"\t\"runtime\": \"java1.8\",\n" +
			"\t\"kind\": \"yr-lib\",\n" +
			"\t\"description\": \"test create\",\n" +
			"\t\"handler\": \"main\",\n" +
			"\t\"cpu\": 500,\n" +
			"\t\"memory\": 500,\n" +
			"\t\"timeout\": 600\n" +
			"}"))
		engine := RegHandlers()
		// create
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions", body)
		if rec.Code != http.StatusOK {
			t.Errorf("%v", rec)
		}
		time.Sleep(1000)
		// get
		_, rec = routerRequest(t, engine, "GET", "/serverless/v1/functions/0-test-aa", body)
		success := successJSONBody{}
		if err := json.Unmarshal(rec.Body.Bytes(), &success); err != nil {
			t.Errorf("%s", err)
		}
		getResponse, ok := success.Result.(map[string]interface{})
		if !ok {
			t.Errorf("failed to get function")
		}
		revisionId := fmt.Sprintf("%v", getResponse["revisionId"])
		assert.NotEqual(t, revisionId, nil)
		time.Sleep(1000)
		// update
		s := strings.ReplaceAll(tt.body, "{}", revisionId)
		body = bytes.NewBuffer([]byte(s))
		_, rec = routerRequest(t, engine, "PUT", tt.path, body)
		fmt.Printf("%v\n", rec)
		assert.Equal(t, tt.expectedCode, rec.Code)
		success = successJSONBody{}
		if err := json.Unmarshal(rec.Body.Bytes(), &success); err != nil {
			t.Errorf("%s", err)
		}
		updateResponse, ok := success.Result.(map[string]interface{})
		if !ok {
			t.Errorf("failed to get function")
		}
		updateRevisionId := fmt.Sprintf("%v", updateResponse["revisionId"])
		t.Logf("revisionId: %s updateRevisionId %s", revisionId, updateRevisionId)
		assert.NotEqual(t, revisionId, updateRevisionId)
	})
}
*/

func Test_getFunctionVersionList(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.GetFunctionVersionList,
		func(ctx server.Context, f string, pi int, ps int) (resp model.FunctionVersionListGetResponse, err error) {
			if f == "0-test-mockerr" {
				return model.FunctionVersionListGetResponse{}, errors.New("mock err")
			}
			return model.FunctionVersionListGetResponse{}, nil
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
			"GET",
			"/serverless/v1/functions/0-test-aa/versions",
			``,
			http.StatusOK,
			"",
		},
		{
			"test upload function code",
			"GET",
			"/serverless/v1/functions/0-test-mockerr/versions",
			``,
			errmsg.InvalidUserParam,
			"",
		},
		{
			"test upload function code with invalid functions",
			"GET",
			"/serverless/v1/functions//versions",
			``,
			errmsg.InvalidQueryURN,
			"",
		},
		{
			"test upload function code with pageIndex and pageSize err",
			"GET",
			"/serverless/v1/functions/0-test-aa/versions?pageIndex=abc&pageSize=abc",
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

func TestGetFunctionList(t *testing.T) {
	// test getFunctionList
	patch := gomonkey.ApplyFunc(service.GetFunctionList, func(
		c server.Context, f string, q string, k string, pi int, ps int,
	) (model.FunctionListGetResponse, error) {
		return model.FunctionListGetResponse{}, nil
	})
	defer patch.Reset()

	body := bytes.NewBuffer([]byte(""))
	engine := RegHandlers()
	_, rec := routerRequest(t, engine, "GET", "/serverless/v1/functions", body)
	assert.Equal(t, rec.Code, http.StatusOK)
}

func TestPublishFunction(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(service.CheckFunctionVersion, func(ctx server.Context, functionName string) error {
		return nil
	})
	patches.ApplyFunc(service.PublishFunction, func(ctx server.Context, funcName string,
		req model.PublishRequest,
	) (model.PublishResponse, error) {
		return model.PublishResponse{
			FunctionVersion: model.FunctionVersion{
				FunctionVersionURN: "sn:cn:yrk:12345678901234561234567890123456:function:0@faaspy@hello:latest",
			},
		}, nil
	})
	body := bytes.NewBuffer([]byte("{\"revisionId\":\"20241217113352215\",\"versionDesc\":\"aaaa\",\"kind\":\"faas\",\"versionNumber\":\"v1\"}"))
	engine := RegHandlers()
	_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/0@faaspy@hello/versions", body)
	assert.Equal(t, rec.Code, http.StatusOK)
	successBody := &successJSONBodyForUser{}
	if err := json.Unmarshal(rec.Body.Bytes(), successBody); err != nil {
		t.Errorf("%s", err)
	}
	functionBytes, _ := json.Marshal(successBody.Function)
	functionResp := &model.GetFunctionResponseForUser{}
	_ = json.Unmarshal(functionBytes, functionResp)
	assert.Equal(t, "sn:cn:yrk:12345678901234561234567890123456:function:0@faaspy@hello:latest", functionResp.ID)
}

func TestReservedInstanceConfig(t *testing.T) {
	type args struct {
		c server.Context
	}
	tests := []struct {
		name         string
		path         string
		createBody   string
		updateBody   string
		delBody      string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test success", "/serverless/v1/functions/reserve-instance",
			"{\"traceId\":\"test123\",\"funcName\":\"0@faaspy@hello\",\"version\":\"latest\",\"tenantId\":\"12345678901234561234567890123456\",\"instanceLabel\":\"label1\",\"instanceConfigInfos\":[{\"clusterId\":\"\",\"maxInstance\":100,\"minInstance\":0}]}",
			"{\"traceId\":\"test123\",\"funcName\":\"0@faaspy@hello\",\"version\":\"latest\"," +
				"\"tenantId\":\"12345678901234561234567890123456\",\"instanceLabel\":\"label1\",\"instanceConfigInfos\":[{\"clusterId\":\"\",\"maxInstance\":99,\"minInstance\":0}]}",
			"{\"traceId\":\"test123\",\"funcName\":\"0@faaspy@hello\",\"version\":\"latest\"," +
				"\"tenantId\":\"12345678901234561234567890123456\",\"instanceLabel\":\"label1\"}",
			http.StatusOK, "",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			body := bytes.NewBuffer([]byte(tt.createBody))
			engine := RegHandlers()
			_, rec := routerRequest(t, engine, "POST", tt.path, body)
			fmt.Printf("%v\n", rec)
			assert.Equal(t, tt.expectedCode, rec.Code)
			body = bytes.NewBuffer([]byte(tt.updateBody))
			_, rec = routerRequest(t, engine, "PUT", tt.path, body)
			fmt.Printf("%v\n", rec)
			assert.Equal(t, tt.expectedCode, rec.Code)
			body = bytes.NewBuffer([]byte(tt.delBody))
			_, rec = routerRequest(t, engine, "DELETE", tt.path, body)
			fmt.Printf("%v\n", rec)
			assert.Equal(t, tt.expectedCode, rec.Code)
		})
	}
}

func TestReserveInstanceInvalidParam(t *testing.T) {
	engine := RegHandlers()
	body := bytes.NewBuffer([]byte("{\"funcName\":\"0@faaspy@hello()\",\"version\":\"v1\",\"instanceLabel\":\"label001\",\"instanceConfigInfos\":[{\"clusterId\":\"cluster001\",\"maxInstance\":101,\"minInstance\":0}]}"))
	_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/reserve-instance", body)
	assert.Equal(t, rec.Code, http.StatusBadRequest)
	body = bytes.NewBuffer([]byte("{\"funcName\":\"0@faaspy@hello\",\"version\":\"v1\"," +
		"\"instanceLabel" +
		"\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" +
		"\",\"instanceConfigInfos\":[{\"clusterId\":\"cluster001\",\"maxInstance\":101," +
		"\"minInstance\":0}]}"))
	_, rec = routerRequest(t, engine, "POST", "/serverless/v1/functions/reserve-instance", body)
	assert.Equal(t, rec.Code, http.StatusBadRequest)
	_, rec = routerRequest(t, engine, "PUT", "/serverless/v1/functions/reserve-instance", body)
	assert.Equal(t, rec.Code, http.StatusBadRequest)
	deleteBody := bytes.NewBuffer([]byte("{]"))
	_, rec = routerRequest(t, engine, "DELETE", "/serverless/v1/functions/reserve-instance", deleteBody)
	assert.Equal(t, rec.Code, http.StatusBadRequest)
	// instanceLabel is empty
	body = bytes.NewBuffer([]byte("{\"funcName\":\"0@faaspy@hello\",\"version\":\"v1\",\"instanceLabel\":\"\",\"instanceConfigInfos\":[{\"clusterId\":\"cluster001\",\"maxInstance\":101,\"minInstance\":0}]}"))
	_, rec = routerRequest(t, engine, "POST", "/serverless/v1/functions/reserve-instance", body)
	assert.Equal(t, rec.Code, http.StatusBadRequest)
}

func TestReserveInstanceFunctionNotFound(t *testing.T) {
	engine := RegHandlers()
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(storage.GetFunctionByFunctionNameAndVersion, func(ctx server.Context, name string,
		version string, kind string,
	) (storage.FunctionVersionValue, error) {
		return storage.FunctionVersionValue{}, errmsg.New(errmsg.FunctionVersionNotFound)
	})
	body := bytes.NewBuffer([]byte("{\"funcName\":\"0@faaspy@hello\",\"version\":\"v1\",\"instanceLabel\":\"label001\",\"instanceConfigInfos\":[{\"clusterId\":\"cluster001\",\"maxInstance\":101,\"minInstance\":0}]}"))
	_, rec := routerRequest(t, engine, "POST", "/serverless/v1/functions/reserve-instance", body)
	assert.Equal(t, rec.Code, http.StatusNotFound)
}
