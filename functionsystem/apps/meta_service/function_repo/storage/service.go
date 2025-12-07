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

package storage

import (
	"strings"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"

	"meta_service/common/constants"
	"meta_service/common/engine"
	"meta_service/common/logger/log"
)

// GetFunctionNamesByServiceID gets function names by service id.
// It returns snerror(4115) if entry does not exists.
func GetFunctionNamesByServiceID(ctx server.Context, sid, kind string) ([]string, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}
	currentDB := GetDB(kind)
	tuples, err := currentDB.FunctionVersionStream(
		ctx.Context(), GenFunctionVersionKey(t, "", ""), engine.SortBy{}).Execute()
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, nil
		}
		log.GetLogger().Errorf("failed to execute function version stream, error: %s", err.Error())
		return nil, err
	}

	set := make(map[string]interface{}, constants.DefaultMapSize)
	for _, tuple := range tuples {
		if !strings.HasPrefix(tuple.Key.FunctionName, sid) {
			continue
		}
		if _, ok := set[tuple.Key.FunctionName]; !ok {
			set[tuple.Key.FunctionName] = struct{}{}
		}
	}

	res := make([]string, 0, len(set))
	for k := range set {
		res = append(res, k)
	}
	return res, nil
}
