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
	"encoding/json"

	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils"
)

const (
	minPageIndex = 1
	maxPageSize  = 1000
	minPageSize  = 1
)

func shouldBindJSON(c server.Context, v interface{}) error {
	if c.Gin().Request == nil || c.Gin().Request.Body == nil {
		return errmsg.New(errmsg.InvalidJSONBody)
	}
	decoder := json.NewDecoder(c.Gin().Request.Body)
	if err := decoder.Decode(v); err != nil {
		log.GetLogger().Errorf("failed to decode body: %s", err.Error())
		if e, ok := err.(snerror.SNError); ok {
			return e
		}
		return errmsg.New(errmsg.InvalidJSONBody)
	}
	return utils.Validate(v)
}
