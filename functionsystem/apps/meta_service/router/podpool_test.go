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
	"fmt"
	"net/http"
	"testing"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"

	"meta_service/common/snerror"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
)

type successJSONBody struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Result  interface{} `json:"result"`
}

func mockCleanEtcd(id string) {
	ctx := server.NewContext(&gin.Context{Request: &http.Request{}})
	txn := storage.NewTxn(ctx)
	storage.DeletePodPool(txn, id)
	txn.Commit()
}

func Test_createPodPool(t *testing.T) {
	type args struct {
		c server.Context
	}
	for i, test := range []struct {
		name             string
		path             string
		body             string
		expectedHttpCode int
		expectedCode     int
		expectedMsg      string
	}{
		{"test bind failed", "/serverless/v1/podpools", "{\n" +
			"\t\"pools\": [[]\n" +
			"}", http.StatusInternalServerError, 130600, "invalid character '}' after array element"},
		{"test create success", "/serverless/v1/podpools", "{\"pools\":[{\"id\":\"pool1\",\"size\":1," +
			"\"group\":\"group1\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true," +
			"\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"}," +
			"\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\",\"horizontal_pod_autoscaler_spec\":\"\",\"pod_pending_duration_threshold\":20,\"runtime_class_name\":\"runc\",\"node_selector\":{\"key1\":\"val1\"},\"tolerations\":\"[]\"}]}", http.StatusOK, 0, ""},
		{"test create failed", "/serverless/v1/podpools", "{\"pools\":[{\"id\":\"\",\"size\":1,\"group\":\"group1\"," +
			"\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"}," +
			"\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"}," +
			"\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\"," +
			"\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\",\"horizontal_pod_autoscaler_spec\":\"\"}," +
			"{\"id\":\"\",\"size\":2,\"group\":\"group2\",\"image\":\"image2\",\"init_image\":\"init2\",\"reuse\":true,\"labels\":{\"p2\":\"p2\"},\"environment\":{\"p2\":\"p2\"},\"annotations\":{\"p2\":\"p2\"},\"resources\":{\"requests\":{\"CPU\":\"600\"},\"limits\":{\"CPU\":\"800\"}},\"volumes\":\"volumes2\",\"volume_mounts\":\"volumes2\",\"pod_affinities\":\"affinity2\",\"horizontal_pod_autoscaler_spec\":\"\"}]}", http.StatusOK, 4104, ""},
		{
			"test create failed because of podPendingDurationThreshold", "/serverless/v1/podpools", "{\"pools\":[{\"id\":\"pool1\",\"size\":1," +
				"\"group\":\"group1\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true," +
				"\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"}," +
				"\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\",\"horizontal_pod_autoscaler_spec\":\"\",\"pod_pending_duration_threshold\":-1,\"runtime_class_name\":\"runc\",\"node_selector\":{\"key1\":\"val1\"},\"tolerations\":\"[]\"}]}",
			http.StatusOK, 4104, "failed to create pod pool, id: pool1, err: podPendingDurationThreshold should not be less than 0, which is -1. check input parameters",
		},
		{
			"test create failed", "/serverless/v1/podpools", "{\"pools\":[{\"id\":\"pool11\",\"size\":1," +
				"\"group\":\"group1\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true," +
				"\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"}," +
				"\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\"," +
				"\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\",\"horizontal_pod_autoscaler_spec\":\"\"," +
				"\"runtime_class_name\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}",
			http.StatusOK, 4104, "",
		},
	} {
		if i == 1 {
			mockCleanEtcd("pool1")
		}
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "POST", test.path, body)
		fmt.Printf("%v\n", rec)
		if rec.Code == http.StatusOK {
			respBody := &successJSONBody{}
			err := json.Unmarshal(rec.Body.Bytes(), &respBody)
			assert.Nil(t, err)
			assert.Equal(t, test.expectedCode, respBody.Code)
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

func Test_deletePodPool(t *testing.T) {
	type args struct {
		c server.Context
	}
	tests := []struct {
		name         string
		path         string
		delBody      string
		createBody   string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test invalid param", "/serverless/v1/podpools", "", "",
			errmsg.InvalidUserParam, "id and group cannot be empty at the same time. check input parameters",
		},
		{
			"test delete failed", "/serverless/v1/podpools?id=pool3", "", "",
			4120, "pool does not exist or has been deleted. check input parameters",
		},
		{
			"test delete failed2", "/serverless/v1/podpools?group=group3", "", "",
			4120, "pool does not exist or has been deleted. check input parameters",
		},
		{
			"test delete succeed", "/serverless/v1/podpools?id=pool4", "", "{\"pools\":[{\"id\":\"pool4\",\"size\":1,\"group\":\"group4\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"pod_affinities\":\"affinity1\"}]}",
			http.StatusOK, "",
		},
	}
	for i := 0; i < 3; i++ {
		tt := tests[i]
		t.Run(tt.name, func(t *testing.T) {
			body := bytes.NewBuffer([]byte(tt.delBody))
			engine := RegHandlers()
			_, rec := routerRequest(t, engine, "DELETE", tt.path, body)
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, tt.expectedCode, badResponse.Code)
			if badResponse.Code == 4120 {
				assert.Equal(t, 404, rec.Code)
			} else {
				assert.Equal(t, 500, rec.Code)
			}
			assert.Equal(t, "application/json; charset=utf-8", rec.Header().Get("Content-Type"))
			if tt.expectedMsg != "" {
				assert.Equal(t, tt.expectedMsg, badResponse.Message)
			}
		})
	}

	tt := tests[3]
	t.Run(tt.name, func(t *testing.T) {
		mockCleanEtcd("pool4")
		body := bytes.NewBuffer([]byte(tt.createBody))
		engine := RegHandlers()
		// create
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/podpools", body)
		// delete
		body = bytes.NewBuffer([]byte(tt.delBody))
		_, rec = routerRequest(t, engine, "DELETE", tt.path, body)
		fmt.Printf("%v\n", rec)
		assert.Equal(t, tt.expectedCode, rec.Code)
	})
}

func Test_getPodPool(t *testing.T) {
	tests := []struct {
		name         string
		method       string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{
			"test get pod pool with empty id and group",
			"GET",
			"/serverless/v1/podpools?limit=5&offset=0",
			``,
			http.StatusOK,
			"",
		},
		{
			"test get pod pool with id failed",
			"GET",
			"/serverless/v1/podpools?id=pool_get&limit=5&offset=0",
			``,
			http.StatusOK,
			"",
		},
		{
			"test get pod pool with group failed",
			"GET",
			"/serverless/v1/podpools?group=group_get1&limit=5&offset=0",
			``,
			http.StatusOK,
			"pod pool group: group_get1 may not exist, try get by id. check input parameters",
		},
		{
			"test get pod pool with id succeed",
			"GET",
			"/serverless/v1/podpools?id=pool_get2&limit=5&offset=0",
			"{\"pools\":[{\"id\":\"pool_get2\",\"size\":1,\"group\":\"group4\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\",\"horizontal_pod_autoscaler_spec\":\"\"}]}",
			http.StatusOK,
			"",
		},
	}
	for i := 0; i < 3; i++ {
		tt := tests[i]
		body := bytes.NewBuffer([]byte(tt.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, tt.method, tt.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, tt.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			assert.Equal(t, "application/json; charset=utf-8", rec.Header().Get("Content-Type"))
			assert.Equal(t, tt.expectedCode, badResponse.Code)
			if tt.expectedMsg != "" {
				assert.Equal(t, tt.expectedMsg, badResponse.Message)
			}
		}
	}

	tt := tests[3]
	t.Run(tt.name, func(t *testing.T) {
		body := bytes.NewBuffer([]byte(tt.body))
		engine := RegHandlers()
		// create
		_, rec := routerRequest(t, engine, "POST", "/serverless/v1/podpools", body)
		// get
		body = bytes.NewBuffer([]byte(""))
		_, rec = routerRequest(t, engine, "GET", tt.path, body)
		fmt.Printf("%v\n", rec)
		assert.Equal(t, tt.expectedCode, rec.Code)
	})
}

func Test_updatePodPool(t *testing.T) {
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
		{"test bind failed", "/serverless/v1/podpools/pool_update1", "{\"size\": [[]}", 130600, "invalid character '}' after array element"},
		{
			"test update failed", "/serverless/v1/podpools/pool_update1", "{\"size\": 5}", 4120,
			"pool does not exist or has been deleted. check input parameters",
		},
	} {
		body := bytes.NewBuffer([]byte(test.body))
		engine := RegHandlers()
		_, rec := routerRequest(t, engine, "PUT", test.path, body)
		if rec.Code == http.StatusOK {
			assert.Equal(t, test.expectedCode, rec.Code)
		} else {
			fmt.Printf("%v\n", rec)
			badResponse := &snerror.BadResponse{}
			if err := json.Unmarshal(rec.Body.Bytes(), badResponse); err != nil {
				t.Errorf("%s", err)
			}
			if badResponse.Code == 4120 {
				assert.Equal(t, 404, rec.Code)
			} else {
				assert.Equal(t, 500, rec.Code)
			}
			assert.Equal(t, "application/json; charset=utf-8", rec.Header().Get("Content-Type"))
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}
