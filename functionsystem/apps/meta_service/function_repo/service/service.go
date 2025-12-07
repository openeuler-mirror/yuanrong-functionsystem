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

package service

import (
	"meta_service/common/constants"
	"meta_service/common/urnutils"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
)

// GetServiceID gets function names by service id
func GetServiceID(ctx server.Context, id, kind string) (model.ServiceGetResponse, error) {
	resp := model.ServiceGetResponse{}
	newID := buildNewServicePrefix(id, kind)
	names, err := storage.GetFunctionNamesByServiceID(ctx, newID, kind)
	if err != nil {
		return model.ServiceGetResponse{}, err
	}
	resp.Total = len(names)
	resp.FunctionNames = names
	return resp, nil
}

func buildNewServicePrefix(id, kind string) (perfix string) {
	if kind == constants.Faas {
		if id == "" {
			return urnutils.FaaSServicePrefix
		}
		return urnutils.FaaSServicePrefix + id + urnutils.DefaultFaaSSeparator
	}
	if id == "" {
		return urnutils.ServicePrefix
	}
	return urnutils.ServicePrefix + id + urnutils.DefaultSeparator
}
