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
	"fmt"
	"io"
	"meta_servicemeta_service/initialize"
	"meta_servicemeta_service/test"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"

	"meta_service/function_repo/server"
)

// TestMain init router test
func TestMain(m *testing.M) {
	if !initialize.Init(test.ConfigPath, "/home/sn/config/log.json") {
		fmt.Println("failed to initialize")
		os.Exit(1)
	}
	test.ResetETCD()
	result := m.Run()
	test.ResetETCD()
	os.Exit(result)
}

func routerRequest(t *testing.T, router server.Engine, method string, path string, body *bytes.Buffer) (*http.Request,
	*httptest.ResponseRecorder,
) {
	req := createRequest(t, method, path, body)
	rec := httptest.NewRecorder()
	rec.Body = new(bytes.Buffer)
	router.ServeHTTP(rec, req)
	return req, rec
}

func createRequest(t *testing.T, method, path string, body io.Reader) *http.Request {
	// HACK: derive content-length since protocol/http does not add content-length
	// if it's not present.
	if body != nil {
		buf := &bytes.Buffer{}
		_, err := io.Copy(buf, body)
		if err != nil {
			t.Fatalf("Test: Could not copy %s request body to %s: %v", method, path, err)
		}
		body = buf
	}

	req, err := http.NewRequest(method, "http://127.0.0.1:8080"+path, body)
	if err != nil {
		t.Fatalf("Test: Could not create %s request to %s: %v", method, path, err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("x-business-id", "yrk")
	req.Header.Set("x-tenant-id", "i1fe539427b24702acc11fbb4e134e17")
	req.Header.Set("x-product-id", "")

	return req
}
