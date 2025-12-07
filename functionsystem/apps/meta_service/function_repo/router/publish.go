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
	"errors"

	"meta_service/common/logger/log"
	"meta_service/function_repo/config"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils"
)

func regPublishHandlers(r server.RouterGroup) {
	r.POST("/:functionName/versions", publishFunction)
}

func publishFunction(c server.Context) {
	funcName := c.Gin().Param("functionName")
	if funcName == "" {
		utils.BadRequest(c, errors.New("empty function name"))
		return
	}
	req := model.PublishRequest{}
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind parameters when publish the function: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}
	info, err := service.ParseFunctionInfo(c, funcName, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    "",
		FunctionType: config.ComFunctionType,
	})
	if err = service.CheckFunctionVersion(c, info.FunctionName); err != nil {
		log.GetLogger().Errorf("failed to check function versions: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}
	resp, err := service.PublishFunction(c, info.FunctionName, req)
	if err != nil {
		log.GetLogger().Errorf("failed to publish function: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}
	utils.WriteResponse(c, resp, err)
}
