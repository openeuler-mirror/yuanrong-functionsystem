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
	"encoding/json"
	"errors"
	"net/http"
	"regexp"
	"strconv"

	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils/constants"

	"meta_service/common/logger/log"
	"meta_service/common/utils"

	common "meta_service/common/constants"

	repoutils "meta_service/function_repo/utils"
)

const (
	contentTypeMaxLen   int = 100
	tokenSplit          int = 2
	faasFunNameReg          = "^0@[0-9a-zA-Z]{1,16}@([a-z0-9][a-z0-9-]{0,126}[a-z0-9]|[a-z])$"
	instanceLabelReg        = "^([A-Za-z0-9][-A-Za-z0-9_.]*)?[A-Za-z0-9]$"
	maxInstanceLabelLen int = 63

	// DefaultTimeout define a defaultTimeout
	DefaultTimeout = 900
)

func regFunctionHandlers(r server.RouterGroup) {
	{
		r.POST("/reserve-instance", reserveInstanceConfCreate)
		r.PUT("/reserve-instance", reserveInstanceConfUpdate)
		r.DELETE("/reserve-instance", reserveInstanceConfDelete)
		r.GET("/reserve-instance", reserveInstanceConfGet)
	}
	{
		r.POST("", createFunction)
		r.DELETE("/:functionName", deleteFunction)
		r.GET("", getFunctionList)
		r.GET("/:functionName", getFunction)
		r.PUT("/:functionName", updateFunction)
	}

	{
		r.POST("/:functionName/versions", publishFunction)
		r.GET("/:functionName/versions", getFunctionVersionList)
	}
}

func handleBadRequestResp(c server.Context, code int, err error) {
	e := repoutils.TransToSnError(err)
	if e.Code() == errmsg.FunctionNotFound {
		repoutils.WriteSnErrorResponseWithJson(c, http.StatusNotFound, e)
		return
	}
	repoutils.WriteSnErrorResponseWithJson(c, code, e)
}

func createFunction(c server.Context) {
	createFunctionInfo(c, config.ComFunctionType)
}

func createFunctionInfo(c server.Context, functionType config.FunctionType) {
	isAdminStr := c.Gin().Query("isAdmin")
	var isAdmin bool
	if isAdminStr == "" {
		isAdmin = false
	} else {
		isAdmin = true
	}
	var req model.FunctionCreateRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind json :%s", err)
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	utils.ValidateTimeout(&req.Timeout, DefaultTimeout)
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: req.Name,
		Qualifier:    "",
		FunctionType: functionType,
	})
	resp, err := service.CreateFunctionInfo(c, req, isAdmin)
	if err != nil {
		repoutils.BadRequest(c, err)
		return
	}
	adaptRsp[model.FunctionVersion](resp, c, isAdmin)
}

func deleteFunction(c server.Context) {
	deleteFunctionInfo(c, config.ComFunctionType)
}

func deleteFunctionInfo(c server.Context, functionType config.FunctionType) {
	fname := c.Gin().Param("functionName")
	// not set default
	qualifier := c.Gin().Query("qualifier")
	versionNumber := c.Gin().Query("versionNumber")
	kind := c.Gin().Query("kind")
	info, err := service.ParseFunctionInfo(c, fname, qualifier)
	if versionNumber != "" {
		info.FunctionVersion = versionNumber
	}
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    info.FunctionVersion,
		FunctionType: functionType,
	})
	tenantInfo, err := c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("get tenantInfo error", err)
	}
	resp, err := service.DeleteResponseForMeta(c, tenantInfo, info.FunctionName, info.FunctionVersion, kind)
	log.GetLogger().Infof("delete %s functions", resp.Total)
	if err != nil {
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	var emptyStruct struct{}
	c.Gin().JSON(http.StatusOK, emptyStruct)
}

func getFunction(c server.Context) {
	getFunctionInfo(c, config.ComFunctionType)
}

func getFunctionInfo(c server.Context, functionType config.FunctionType) {
	fname := c.Gin().Param("functionName")
	qualifier := c.Gin().Query("qualifier")
	versionNumber := c.Gin().Query("versionNumber")
	isAdminStr := c.Gin().Query("isAdmin")
	var isAdmin bool
	if isAdminStr == "" {
		isAdmin = false
	} else {
		isAdmin = true
	}
	kind := c.Gin().Query("kind")
	log.GetLogger().Infof("get function info, fname: %s, qualifier: %s, versionNumber: %s, kind: %s", fname,
		qualifier, versionNumber, kind)
	info, err := service.ParseFunctionInfo(c, fname, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	if versionNumber != "" {
		info.FunctionVersion = versionNumber
	}
	req := model.FunctionGetRequest{
		FunctionName:    info.FunctionName,
		FunctionVersion: info.FunctionVersion,
		AliasName:       info.AliasName,
		Kind:            kind,
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionVersion,
		Qualifier:    info.AliasName,
	})
	c = initResourceInfo(c, info, functionType)
	resp, err := service.GetFunction(c, req)
	if err != nil {
		log.GetLogger().Errorf("failed to GetFunction :%s", err)
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	adaptRsp[model.FunctionGetResponse](resp, c, isAdmin)
}

func adaptRsp[T model.FunctionGetResponse | model.FunctionVersion](resp T, c server.Context, isAdmin bool) {
	jsonStr, err := json.Marshal(resp)
	if err != nil {
		log.GetLogger().Errorf("failed to json.Marshal :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	var repForUser model.GetFunctionResponseForUser
	err = json.Unmarshal(jsonStr, &repForUser)
	if err != nil {
		log.GetLogger().Errorf("failed to json.Unmarshal :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	if repForUser.LastModified == "" {
		repForUser.LastModified = repForUser.Created
	}
	repForUser.ID = repForUser.FunctionVersionURN
	if isAdmin {
		repoutils.WriteResponse(c, resp, err)
	} else {
		c.Gin().JSON(http.StatusOK, successJSONBodyForUser{
			Message:  "SUCCESS",
			Function: repForUser,
		})
	}
}

func getFunctionList(c server.Context) {
	fname := c.Gin().Query("name")
	qualifier := c.Gin().Query("qualifier")
	versionNumber := c.Gin().Query("versionNumber")
	if versionNumber != "" {
		// if versionNumber specified, use version only
		qualifier = versionNumber
	}
	kind := c.Gin().Query("kind")
	pageIndex, pageSize := parsePageParam(c)
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName:          fname,
		Qualifier:             qualifier,
		FunctionVersionNumber: versionNumber,
	})
	log.GetLogger().Infof("get function list, fname: %s, qualifier: %s, kind: %s, pageIndex: %d, "+
		"pageSize: %d",
		fname, qualifier, kind, pageIndex, pageSize)
	resp, err := service.GetFunctionList(c, fname, qualifier, kind, pageIndex, pageSize)
	repoutils.WriteResponse(c, resp, err)
}

func getFunctionVersionList(c server.Context) {
	fname := c.Gin().Param("functionName")
	isAdminStr := c.Gin().Query("isAdmin")
	var isAdmin bool
	if isAdminStr == "" {
		isAdmin = false
	} else {
		isAdmin = true
	}
	pageIndex, pageSize := parsePageParam(c)
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: fname,
		Qualifier:    "",
	})
	info, err := service.ParseFunctionInfo(c, fname, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	resp, err := service.GetFunctionVersionList(c, info.FunctionName, pageIndex, pageSize)
	if err != nil {
		log.GetLogger().Errorf("failed to get function version list :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	adaptRspForVersionList(resp, c, isAdmin, pageIndex, pageSize)
}

func parsePageParam(c server.Context) (int, int) {
	pageIndex, err := strconv.Atoi(c.Gin().DefaultQuery("pageIndex", constants.DefaultPageIndex))
	if err != nil {
		log.GetLogger().Warnf("failed to get pageIndex")
	}
	pageSize, err := strconv.Atoi(c.Gin().DefaultQuery("pageSize", constants.DefaultPageSize))
	if err != nil {
		log.GetLogger().Warnf("failed to get pageSize")
	}
	return pageIndex, pageSize
}

func adaptRspForVersionList(resp model.FunctionVersionListGetResponse, c server.Context, isAdmin bool,
	pageIndex int, pageSize int,
) {
	resForUser := model.GetFunctionVersionsResponse{
		Functions: nil,
		PageInfo: model.PageInfo{
			PageIndex: strconv.Itoa(pageIndex),
			PageSize:  strconv.Itoa(pageSize),
			Total:     uint64(resp.Total),
		},
	}
	var err error
	for _, value := range resp.Versions {
		var t model.GetFunctionResponse
		raw, err := json.Marshal(value)
		err = json.Unmarshal(raw, &t)
		if err != nil {
			log.GetLogger().Errorf("failed to unmarshal, err: %s", err.Error())
			continue
		}
		if t.LastModified == "" {
			t.LastModified = t.Created
		}
		t.ID = t.FunctionVersionURN
		resForUser.Functions = append(resForUser.Functions, t)
	}

	if isAdmin {
		repoutils.WriteResponse(c, resp, err)
	} else {
		c.Gin().JSON(http.StatusOK, resForUser)
	}
}

func updateFunction(c server.Context) {
	updateFunctionInfo(c, config.ComFunctionType)
}

func updateFunctionInfo(c server.Context, functionType config.FunctionType) {
	fname := c.Gin().Param("functionName")
	isAdminStr := c.Gin().Query("isAdmin")
	var isAdmin bool
	if isAdminStr == "" {
		isAdmin = false
	} else {
		isAdmin = true
	}
	log.GetLogger().Infof("update function info, function name: %s", fname)
	req := model.FunctionUpdateRequest{}
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind parameters when updating the function %s", err)
		repoutils.BadRequest(c, err)
		return
	}
	defaultVersion := repoutils.GetDefaultVersion()
	if req.Kind == constants.Faas {
		defaultVersion = repoutils.GetDefaultFaaSVersion()
	}
	info, err := service.ParseFunctionInfo(c, fname, defaultVersion)
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		repoutils.BadRequest(c, err)
		return
	}
	c.InitResourceInfo(server.ResourceInfo{
		FunctionType: functionType,
	})
	resp, err := service.UpdateFunctionInfo(c, info, req, isAdmin)
	if err != nil {
		log.GetLogger().Errorf("failed to update the function %s", err)
		repoutils.BadRequest(c, err)
		return
	}

	repoutils.WriteResponse(c, resp, err)
}

func initResourceInfo(c server.Context, info model.FunctionQueryInfo,
	functionType config.FunctionType,
) server.Context {
	resourceInfo := server.ResourceInfo{
		ResourceName: info.FunctionName,
	}
	if info.FunctionVersion != "" {
		resourceInfo.Qualifier = info.FunctionVersion
	} else {
		resourceInfo.Qualifier = info.AliasName
	}
	resourceInfo.FunctionType = functionType
	c.InitResourceInfo(resourceInfo)
	return c
}

func publishFunction(c server.Context) {
	isAdminStr := c.Gin().Query("isAdmin")
	var isAdmin bool
	if isAdminStr == "" {
		isAdmin = false
	} else {
		isAdmin = true
	}
	funcName := c.Gin().Param("functionName")
	if funcName == "" {
		repoutils.BadRequest(c, errors.New("empty function name"))
		return
	}
	req := model.PublishRequest{}
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind parameters when publish the function: %s", err.Error())
		repoutils.BadRequest(c, err)
		return
	}
	info, err := service.ParseFunctionInfo(c, funcName, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info: %s", err.Error())
		repoutils.BadRequest(c, err)
		return
	}
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    "",
		FunctionType: config.ComFunctionType,
	})
	if err = service.CheckFunctionVersion(c, info.FunctionName); err != nil {
		log.GetLogger().Errorf("failed to check function versions: %s", err.Error())
		repoutils.BadRequest(c, err)
		return
	}
	resp, err := service.PublishFunction(c, info.FunctionName, req)
	if err != nil {
		log.GetLogger().Errorf("failed to publish function: %s", err.Error())
		repoutils.BadRequest(c, err)
		return
	}
	adaptRsp[model.FunctionGetResponse](resp, c, isAdmin)
}

func validateReserveInsRequest(funcName, instanceLabel string, labelCanBeEmpty bool) error {
	isMatch, err := regexp.MatchString(faasFunNameReg, funcName)
	if err != nil || !isMatch {
		return errmsg.NewParamError("funcName format is 0@{serviceName}@{funcName};serviceName can only contains" +
			"" + " letters、digits and cannot exceed 16 characters; funcName can only contains lowercase letters" +
			"、digits、- and cannot exceed 127 characters or end with -")
	}
	if len(instanceLabel) == 0 {
		if labelCanBeEmpty {
			return nil
		} else {
			return errmsg.NewParamError("instanceLabel cannot be empty")
		}
	}
	isMatch, err = regexp.MatchString(instanceLabelReg, instanceLabel)
	if err != nil || !isMatch {
		return errmsg.NewParamError("instanceLabel can only contains letters、digits、hyphens、dots,not exceed 63" +
			" characters")
	}
	return nil
}

func reserveInstanceConfCreate(c server.Context) {
	req := model.CreateReserveInsRequest{}
	req.TenantID = c.Gin().GetHeader(common.HeaderTenantID)
	req.TraceID = c.Gin().GetHeader(common.HeaderTraceID)
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("%s|failed to bind parameters when create reserve instance config: %s",
			req.TraceID, err.Error())
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	if err := validateReserveInsRequest(req.FuncName, req.InstanceLabel, false); err != nil {
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	resp, err := service.CreateReserveInstance(c, req, false)
	if err != nil {
		log.GetLogger().Errorf("failed to create reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	c.Gin().JSON(http.StatusOK, resp)
}

func reserveInstanceConfUpdate(c server.Context) {
	req := model.CreateReserveInsRequest{}
	req.TenantID = c.Gin().GetHeader(common.HeaderTenantID)
	req.TraceID = c.Gin().GetHeader(common.HeaderTraceID)
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("%s|failed to bind parameters when update reserve instance config: %s",
			req.TraceID, err.Error())
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	if err := validateReserveInsRequest(req.FuncName, req.InstanceLabel, false); err != nil {
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	resp, err := service.CreateReserveInstance(c, req, true)
	if err != nil {
		log.GetLogger().Errorf("failed to update reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	c.Gin().JSON(http.StatusOK, resp)
}

func reserveInstanceConfDelete(c server.Context) {
	req := model.DeleteReserveInsRequest{}
	req.TenantID = c.Gin().GetHeader(common.HeaderTenantID)
	req.TraceID = c.Gin().GetHeader(common.HeaderTraceID)
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind parameters when delete reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	if err := validateReserveInsRequest(req.FuncName, req.InstanceLabel, true); err != nil {
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	resp, err := service.DeleteReserveInstance(c, req)
	if err != nil {
		log.GetLogger().Errorf("failed to delete reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	c.Gin().JSON(http.StatusOK, resp)
}

func reserveInstanceConfGet(c server.Context) {
	req := model.GetReserveInsRequest{}
	req.TenantID = c.Gin().GetHeader(common.HeaderTenantID)
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind parameters when get reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	if err := validateReserveInsRequest(req.FuncName, "", true); err != nil {
		handleBadRequestResp(c, http.StatusBadRequest, err)
		return
	}
	resp, err := service.GetReserveInstanceConfigs(c, req)
	if err != nil {
		log.GetLogger().Errorf("failed to get reserve instance config: %s", err.Error())
		handleBadRequestResp(c, http.StatusInternalServerError, err)
		return
	}
	c.Gin().JSON(http.StatusOK, resp)
}
