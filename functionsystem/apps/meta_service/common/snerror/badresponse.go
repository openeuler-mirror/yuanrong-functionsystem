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

// Package snerror is basic information contained in the SN error.
package snerror

import (
	"encoding/json"
	"fmt"
)

// BadResponse HTTP request message that does not return 200
type BadResponse struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

// ConvertError Convert SNError to BadResponse
func ConvertError(error SNError) BadResponse {
	return BadResponse{
		Code:    error.Code(),
		Message: error.Error(),
	}
}

// Marshal Marshal SNError to byte
func Marshal(error SNError) []byte {
	b, e := json.Marshal(ConvertError(error))
	if e != nil {
		return []byte(fmt.Sprintf("marshal snError failed %s.snerror/badresponse:33", e))
	}
	return b
}

// ConvertBadResponse Convert BadResponse body to error
func ConvertBadResponse(badResponseBody []byte) error {
	badResponse := &BadResponse{}
	if err := json.Unmarshal(badResponseBody, badResponse); err != nil {
		return err
	}
	return New(badResponse.Code, badResponse.Message)
}
