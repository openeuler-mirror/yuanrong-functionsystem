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
	"strconv"

	"meta_service/common/logger/log"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils"
	"meta_service/function_repo/utils/constants"
)

func regAliasHandler(r server.RouterGroup) {
	r.POST("/:functionName/aliases", createFunctionAlias)
	r.DELETE("/:functionName/aliases/:name", deleteFunctionAlias)
	r.GET("/:functionName/aliases/:name", getFunctionAlias)
	r.GET("/:functionName/aliases", getFunctionAliasesList)
	r.PUT("/:functionName/aliases/:name", updateFunctionAlias)
}

func createFunctionAlias(c server.Context) {
	fName := c.Gin().Param("functionName")
	info, err := service.ParseFunctionInfo(c, fName, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info, error: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	var req model.AliasRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to parse request, error: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	log.GetLogger().Infof("create alias req: %+v", req)

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    req.Name,
	})
	resp, err := service.CreateAlias(c, info.FunctionName, req)
	if err != nil {
		log.GetLogger().Errorf("failed to create alias, error: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}
	utils.WriteResponse(c, resp, err)
}

func updateFunctionAlias(c server.Context) {
	fName := c.Gin().Param("functionName")
	aName := c.Gin().Param("name")
	info, err := service.ParseFunctionInfo(c, fName, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info, error: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	var req model.AliasUpdateRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to parse request, error: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	log.GetLogger().Infof("update alias req: %+v", req)

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    aName,
	})

	resp, err := service.UpdateAlias(c, info.FunctionName, aName, req)
	utils.WriteResponse(c, resp, err)
}

func deleteFunctionAlias(c server.Context) {
	fName := c.Gin().Param("functionName")
	aName := c.Gin().Param("name")
	info, err := service.ParseFunctionInfo(c, fName, "")
	if err != nil {
		utils.BadRequest(c, err)
		return
	}

	req := model.AliasDeleteRequest{
		FunctionName: info.FunctionName,
		AliasName:    aName,
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    aName,
	})
	err = service.DeleteAlias(c, req)
	utils.WriteResponse(c, nil, err)
}

func getFunctionAlias(c server.Context) {
	fname := c.Gin().Param("functionName")
	aname := c.Gin().Param("name")
	info, err := service.ParseFunctionInfo(c, fname, "")
	if err != nil {
		utils.BadRequest(c, err)
		return
	}

	req := model.AliasQueryRequest{
		FunctionName: info.FunctionName,
		AliasName:    aname,
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    info.FunctionVersion,
	})
	resp, err := service.GetAlias(c, req)
	utils.WriteResponse(c, resp, err)
}

func getFunctionAliasesList(c server.Context) {
	fname := c.Gin().Param("functionName")
	fversion := c.Gin().Query("functionVersion")

	pageIndexStr := c.Gin().DefaultQuery("pageIndex", constants.DefaultPageIndex)
	pageIndex, err := strconv.Atoi(pageIndexStr)
	if err != nil {
		utils.BadRequest(c, err)
		return
	}
	pageSizeStr := c.Gin().DefaultQuery("pageSize", constants.DefaultPageSize)
	pageSize, err := strconv.Atoi(pageSizeStr)
	if err != nil || pageSize > maxPageSize || pageSize < minPageSize {
		utils.BadRequest(c, err)
		return
	}

	info, err := service.ParseFunctionInfo(c, fname, "")
	if err != nil {
		utils.BadRequest(c, err)
		return
	}

	req := model.AliasListQueryRequest{
		FunctionName:    info.FunctionName,
		FunctionVersion: fversion,
		PageIndex:       pageIndex,
		PageSize:        pageSize,
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionVersion,
		Qualifier:    "",
	})
	resp, err := service.GetAliaseList(c, req)
	utils.WriteResponse(c, resp, err)
}
