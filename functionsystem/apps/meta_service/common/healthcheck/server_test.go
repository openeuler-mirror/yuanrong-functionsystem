/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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
package healthcheck

import (
	"github.com/gin-gonic/gin"
	"net/http"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

const waitTime = 500

func TestStartServeCase1(t *testing.T) {
	go StartServe("127.0.0.1", nil)
	tchan := time.After(waitTime * time.Millisecond)
	<-tchan

	rsp, err := http.Get("http://localhost:8090/healthz")
	defer rsp.Body.Close()
	assert.Nil(t, err, "get error ", err)
	assert.Equal(t, http.StatusOK, rsp.StatusCode, "resp status: ", rsp.Status)

	err = StartServeTLS("127.0.0.1", "", "", "", nil)
	assert.NotNil(t, err, "err is nil")
}

func TestStartServeCase2(t *testing.T) {
	testFunc := func(c *gin.Context) {}
	go StartServe("127.0.0", testFunc)
}
