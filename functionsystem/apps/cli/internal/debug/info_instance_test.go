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

package debug

import (
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"google.golang.org/protobuf/proto"

	message "cli/internal/pb"
	"cli/pkg/cmdio"
	"cli/utils"
)

func TestTransStructsToString(t *testing.T) {
	infos := []InstanceInfo{
		{"inst1", 101, "127.0.0.1:8080", "S"},
		{"inst2", 102, "127.0.0.1:8081", "R"},
		{"inst3", 103, "127.0.0.1:8082", "S"},
	}

	printedFields := []string{"InstanceID", "Status"}
	toString, _ := transStructsToString(infos, printedFields)
	assert.Equal(t, "inst1,S\ninst2,R\ninst3,S", toString)
}

func TestQueryDebugInstanceInfo(t *testing.T) {
	info := &message.DebugInstanceInfo{
		InstanceID:  "instance1",
		Pid:         101,
		DebugServer: "127.0.0.1:8080",
		Status:      "R",
	}
	debugInstanceInfos := []*message.DebugInstanceInfo{info}
	mockRsp := message.QueryDebugInstanceInfosResponse{
		RequestID:          "req1",
		Code:               0,
		DebugInstanceInfos: debugInstanceInfos,
	}
	// 创建一个处理 HTTP 请求的路由器
	mux := http.NewServeMux()
	mux.HandleFunc("/instance-manager/query-debug-instances", func(w http.ResponseWriter, r *http.Request) {
		data, err := proto.Marshal(&mockRsp)
		if err != nil {
			http.Error(w, "Failed to marshal protobuf", http.StatusInternalServerError)
			return
		}
		// 设置响应类型为 "application/x-protobuf"
		w.Header().Set("Content-Type", "application/x-protobuf")
		// 写入响应数据
		w.Write(data)
	})
	// 创建一个模拟 HTTP 服务器
	server := httptest.NewServer(mux)
	defer server.Close()

	cmdIO := &cmdio.CmdIO{
		In:     os.Stdin,
		Out:    os.Stdout,
		ErrOut: os.Stderr,
	}
	// server.URL eg:  http://127.0.0.1:39341
	parts := strings.Split(server.URL, ":")
	MasterInfo = &utils.MasterInfo{
		MasterIP:            parts[1][2:],
		GlobalSchedulerPort: parts[2],
	}

	err := queryDebugInstanceInfo(cmdIO)

	assert.Nil(t, err)
	assert.Contains(t, debugInstanceInfosMap, "instance1")
	assert.Equal(t, debugInstanceInfosMap["instance1"].PID, int32(101))
}
