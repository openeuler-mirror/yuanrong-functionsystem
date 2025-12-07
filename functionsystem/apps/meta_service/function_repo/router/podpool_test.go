/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"

	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
)

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
		name         string
		path         string
		body         string
		expectedCode int
		expectedMsg  string
	}{
		{"test bind failed", "/function-repository/v1/podpools", "{\n" +
			"\t\"pools\": [[]\n" +
			"}", 121052, "request body is not a valid JSON object"},
		{"test create success", "/function-repository/v1/podpools", "{\n" +
			"\t\"pools\": [{\"id\":\"pool1\",\"size\":1,\"group\":\"group1\",\"image\":\"image1\"," +
			"\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"}," +
			"\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\"," +
			"\"volume_mounts\":\"volumes\",\"topology_spread_constraints\":\"[{}]\"," +
			"\"affinities\":\"affinity1\"}]\n" +
			"}", http.StatusOK, ""},
		{"test create name error length", "/function-repository/v1/podpools", "{\n" +
			"\t\"pools\": [{\"id\":\"123e4567-e89b-12d3-a456-4266554400001234\",\"size\":1,\"group\":\"group1\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"affinities\":\"affinity1\"}]\n" +
			"}", http.StatusOK, ""},
		{"test create failed", "/function-repository/v1/podpools", "{\n" +
			"\t\"pools\": [{\"id\":\"\",\"size\":1,\"group\":\"group1\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"pod_anti_affinities\":\"affinity1\"},{\"id\":\"\",\"size\":2,\"group\":\"group2\",\"image\":\"image2\",\"init_image\":\"init2\",\"reuse\":true,\"labels\":{\"p2\":\"p2\"},\"environment\":{\"p2\":\"p2\"},\"annotations\":{\"p2\":\"p2\"},\"resources\":{\"requests\":{\"CPU\":\"600\"},\"limits\":{\"CPU\":\"800\"}},\"volumes\":\"volumes2\",\"volume_mounts\":\"volumes2\",\"affinities\":\"affinity2\"}]\n" +
			"}", http.StatusOK, ""},
	} {
		if i == 1 {
			mockCleanEtcd("pool1")
		}
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
		{"test invalid param", "/function-repository/v1/podpools", "", "",
			errmsg.InvalidUserParam, "id and group cannot be empty at the same time. check input parameters"},
		{"test delete failed", "/function-repository/v1/podpools?id=pool3", "", "",
			4120, "pool does not exist or has been deleted. check input parameters"},
		{"test delete failed2", "/function-repository/v1/podpools?group=group3", "", "",
			4120, "pool does not exist or has been deleted. check input parameters"},
		{"test delete succeed", "/function-repository/v1/podpools?id=pool4", "", "{\n" +
			"\t\"pools\": [{\"id\":\"pool4\",\"size\":1,\"group\":\"group4\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"pod_affinities\":\"affinity1\"}]\n" +
			"}",
			http.StatusOK, ""},
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
		_, rec := routerRequest(t, engine, "POST", "/function-repository/v1/podpools", body)
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
			"/function-repository/v1/podpools?limit=5&offset=0",
			``,
			http.StatusOK,
			"",
		},
		{
			"test get pod pool with id failed",
			"GET",
			"/function-repository/v1/podpools?id=pool_get&limit=5&offset=0",
			``,
			http.StatusOK,
			"",
		},
		{
			"test get pod pool with group failed",
			"GET",
			"/function-repository/v1/podpools?group=group_get1&limit=5&offset=0",
			``,
			http.StatusOK,
			"pod pool group: group_get1 may not exist, try get by id. check input parameters",
		},
		{
			"test get pod pool with id succeed",
			"GET",
			"/function-repository/v1/podpools?id=pool_get2&limit=5&offset=0",
			"{\n" +
				"\t\"pools\": [{\"id\":\"pool_get2\",\"size\":1,\"group\":\"group4\",\"image\":\"image1\",\"init_image\":\"init1\",\"reuse\":true,\"labels\":{\"p1\":\"p1\"},\"environment\":{\"p1\":\"p1\"},\"annotations\":{\"p1\":\"p1\"},\"resources\":{\"requests\":{\"CPU\":\"500\"},\"limits\":{\"CPU\":\"600\"}},\"volumes\":\"volumes\",\"volume_mounts\":\"volumes\",\"pod_affinities\":\"affinity1\"}]\n" +
				"}",
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
		_, rec := routerRequest(t, engine, "POST", "/function-repository/v1/podpools", body)
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
		{"test bind failed", "/function-repository/v1/podpools/pool_update1", "{\"size\": [[]}", 121052, "request body is not a valid JSON object"},
		{"test update failed", "/function-repository/v1/podpools/pool_update1", "{\"size\": 5}", 4120,
			"pool does not exist or has been deleted. check input parameters"},
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
			assert.Equal(t, test.expectedCode, badResponse.Code)
			if test.expectedMsg != "" {
				assert.Equal(t, test.expectedMsg, badResponse.Message)
			}
		}
	}
}
