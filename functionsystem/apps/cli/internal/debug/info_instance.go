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
	"bufio"
	"fmt"
	"io"
	"net/http"
	"reflect"
	"strings"
	"time"

	"github.com/olekukonko/tablewriter"
	"google.golang.org/protobuf/proto"

	"cli/internal/pb"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

// InstanceInfo for debug instance info
type InstanceInfo struct {
	InstanceID  string `json:"instanceID"`
	PID         int32  `json:"pid"`
	DebugServer string `json:"debugServer"`
	Status      string `json:"status"`
}

var debugInstanceInfosMap map[string]InstanceInfo

var httpClient *http.Client

var pageSize = 5

var printedFields = []string{"InstanceID", "Status"}

func init() {
	httpClient = &http.Client{
		Timeout: 30 * time.Minute, // 连接超时时间
		Transport: &http.Transport{
			MaxIdleConns:        10,               // 最大空闲连接数
			MaxIdleConnsPerHost: 5,                // 每个主机最大空闲连接数
			MaxConnsPerHost:     10,               // 每个主机最大连接数
			IdleConnTimeout:     30 * time.Second, // 空闲连接的超时时间
			TLSHandshakeTimeout: 30 * time.Minute, // 限制TLS握手的时间
		},
	}
}

const listDebugInstPath = "/instance-manager/query-debug-instances"

func pageOutDebugInstanceInfos(cmdIO *cmdio.CmdIO) {
	err := queryDebugInstanceInfo(cmdIO)
	if err != nil {
		return
	}
	var values []InstanceInfo
	for _, v := range debugInstanceInfosMap {
		values = append(values, v)
	}
	dataStr, err := transStructsToString(values, printedFields)
	if err != nil {
		return
	}

	printTable(dataStr, cmdIO, printedFields)
}

// 执行info instance后通过restful接口查询instance信息
func queryDebugInstanceInfo(cmdIO *cmdio.CmdIO) error {
	url := "http://" + MasterInfo.MasterIP + ":" + MasterInfo.GlobalSchedulerPort
	body, err := requestFunctionMaster(url, listDebugInstPath)
	if err != nil {
		colorprint.PrintFail(cmdIO.Out, "request master to get debug info failed: ", err.Error(), "\n")
		return err
	}
	var debugInst pb.QueryDebugInstanceInfosResponse
	err = proto.Unmarshal(body, &debugInst)
	if err != nil {
		colorprint.PrintFail(cmdIO.Out, "response body to QueryDebugInstancesInfoResponse failed: ", err.Error(), "\n")
		return err
	}
	pbToInstanceInfos(debugInst.DebugInstanceInfos)
	return nil
}

// 请求master获取序列化数据
func requestFunctionMaster(url string, path string) ([]byte, error) {
	req, err := http.NewRequest("GET", url+path, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Type", "protobuf")
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	return body, nil
}

// proto消息体反序列化并转存到DebugInstanceInfosMap中
func pbToInstanceInfos(pbInfos []*pb.DebugInstanceInfo) {
	debugInstanceInfosMap = make(map[string]InstanceInfo)
	for _, pbInfo := range pbInfos {
		instId := pbInfo.InstanceID
		info := InstanceInfo{
			InstanceID:  instId,
			PID:         pbInfo.Pid,
			DebugServer: pbInfo.DebugServer,
			Status:      pbInfo.Status,
		}
		debugInstanceInfosMap[instId] = info
	}
}

// 结构体转为长字符串，结构体字符串之间用","进行分割
func transStructsToString(arr interface{}, fieldNames []string) (string, error) {
	val := reflect.ValueOf(arr)
	if val.Kind() != reflect.Slice {
		return "", fmt.Errorf("expected a slice, got %s", val.Kind())
	}
	var result []string
	for i := 0; i < val.Len(); i++ {
		structVal := val.Index(i)
		if structVal.Kind() != reflect.Struct {
			return "", fmt.Errorf("expected struct element, got %s", structVal.Kind())
		}
		var fields []string
		for _, fieldName := range fieldNames {
			field := structVal.FieldByName(fieldName)
			if !field.IsValid() {
				return "", fmt.Errorf("field %s not found in struct", fieldName)
			}
			fields = append(fields, fmt.Sprintf("%v", field.Interface()))
		}
		result = append(result, strings.Join(fields, ","))
	}
	return strings.Join(result, "\n"), nil
}

func printTable(dataStr string, cmdIO *cmdio.CmdIO, tableHeader []string) {
	dataLines := strings.Split(strings.TrimSpace(dataStr), "\n")
	// 计算总页数
	pageNum := (len(dataLines) + pageSize - 1) / pageSize
	// 绑定标准输出
	table := utils.NewTable(cmdIO.Out, true, true, true)
	table.SetAlignment(tablewriter.ALIGN_CENTER)
	table.SetHeader(tableHeader)
	input := "c" // 执行info instance后打印第一页的info
	var err error
	reader := bufio.NewReader(cmdIO.In)
	for i := 0; i < pageNum; i++ {
		pageStart := i * pageSize
		pageEnd := pageStart + pageSize
		if pageEnd > len(dataLines) {
			pageEnd = len(dataLines)
		}
		if input == "c" || input == "continue" {
			for j := pageStart; j < pageEnd; j++ {
				table.Append(strings.Split(dataLines[j], ","))
			}
			table.Render()
			table.ClearRows()
		} else if input == "q" || input == "quit" {
			break
		} else {
			colorprint.PrintInteractive(cmdIO.Out, "Invalid cmd. Type q or quit to quit, "+
				"c or continue to show next page.\n")
			i-- // 防止在输错命令后下一页被跳过
		}
		// 判断是否还有完成所有instance info浏览
		if pageEnd == len(dataLines) {
			break
		}
		msg := fmt.Sprintf("Page [%d/%d]. Type q or quit to quit, c or continue to show next page.\n", i, pageNum)
		colorprint.PrintInteractive(cmdIO.Out, msg)
		// 获取用户接下来的指令输入
		input, err = reader.ReadString('\n')
		if err != nil {
			colorprint.PrintFail(cmdIO.Out, "Failed to read input: ", err.Error(), "\n")
			break
		}
		input = strings.ToLower(strings.TrimSpace(input))
	}
}
