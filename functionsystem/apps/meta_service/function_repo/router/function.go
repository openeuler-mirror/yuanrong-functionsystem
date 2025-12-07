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
	"strconv"
	"strings"

	common "meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/function_repo/config"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils"
	"meta_service/function_repo/utils/constants"
)

const (
	contentTypeMaxLen int = 100
	tokenSplit        int = 2
)

func regFunctionHandlers(r server.RouterGroup) {
	r.DELETE("/:functionName/code", deleteFunctionCode)
	r.PUT("/:functionName/code", uploadFunctionCode)
}

func deleteFunctionCode(c server.Context) {
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
		utils.BadRequest(c, err)
		return
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    info.FunctionVersion,
		FunctionType: config.ComFunctionType,
	})

	err = service.DeleteResponse(c, info.FunctionName, info.FunctionVersion, kind)
	utils.WriteResponse(c, nil, err)
}

func getFunction(c server.Context) {
	getFunctionInfo(c, config.ComFunctionType)
}

func getFunctionInfo(c server.Context, functionType config.FunctionType) {
	fname := c.Gin().Param("functionName")
	qualifier := c.Gin().Query("qualifier")
	versionNumber := c.Gin().Query("versionNumber")
	kind := c.Gin().Query("kind")
	log.GetLogger().Infof("get function info, fname: %s, qualifier: %s, versionNumber: %s, kind: %s", fname,
		qualifier, versionNumber, kind)
	info, err := service.ParseFunctionInfo(c, fname, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		utils.BadRequest(c, err)
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
	utils.WriteResponse(c, resp, err)
}

func getFunctionList(c server.Context) {
	fname := c.Gin().Query("functionName")
	qualifier := c.Gin().Query("qualifier")
	versionNumber := c.Gin().Query("versionNumber")
	if versionNumber != "" {
		// if versionNumber specified, use version only
		qualifier = versionNumber
	}
	kind := c.Gin().Query("kind")
	pageIndex, err := strconv.Atoi(c.Gin().DefaultQuery("pageIndex", constants.DefaultPageIndex))
	if err != nil {
		log.GetLogger().Warnf("failed to get pageIndex")
	}
	pageSize, err := strconv.Atoi(c.Gin().DefaultQuery("pageSize", constants.DefaultPageSize))
	if err != nil {
		log.GetLogger().Warnf("failed to get pageSize")
	}
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName:          fname,
		Qualifier:             qualifier,
		FunctionVersionNumber: versionNumber,
	})
	log.GetLogger().Infof("get function list, fname: %s, qualifier: %s, kind: %s, pageIndex: %d, "+
		"pageSize: %d",
		fname, qualifier, kind, pageIndex, pageSize)
	resp, err := service.GetFunctionList(c, fname, qualifier, kind, pageIndex, pageSize)
	utils.WriteResponse(c, resp, err)
}

func uploadFunctionCode(c server.Context) {
	uploadFunctionInfoCode(c, config.ComFunctionType)
}

func uploadFunctionInfoCode(c server.Context, functionType config.FunctionType) {
	fname := c.Gin().Param("functionName")
	version := c.Gin().Request.Header.Get("X-Version")
	kind := c.Gin().Request.Header.Get("X-Kind")
	info, err := service.ParseFunctionInfo(c, fname, version)
	if err != nil {
		log.GetLogger().Errorf("failed to get function query info :%s", err)
		utils.BadRequest(c, err)
		return
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.FunctionName,
		Qualifier:    "",
		FunctionType: functionType,
	})

	contentType := c.Gin().Request.Header.Get("Content-Type")
	m, err := getFileInfo(contentType)
	if err != nil {
		log.GetLogger().Errorf("failed to get file info :%s", err)
		utils.BadRequest(c, err)
		return
	}

	fileSize, err := strconv.ParseInt(m["file-size"], 10, 0)
	if err != nil {
		log.GetLogger().Errorf("failed to parse file size:%s", err)
		utils.BadRequest(c, err)
		return
	}
	req := model.FunctionCodeUploadRequest{
		RevisionID: m["revision-id"],
		Kind:       kind,
		FileSize:   fileSize,
	}
	err = utils.Validate(req)
	if err != nil {
		log.GetLogger().Errorf("failed to validate:%s", err)
		utils.BadRequest(c, err)
		return
	}
	resp, err := service.GetUploadFunctionCodeResponse(c, info, req)
	utils.WriteResponse(c, resp, err)
}

func initResourceInfo(c server.Context, info model.FunctionQueryInfo,
	functionType config.FunctionType) server.Context {
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

func getFileInfo(contentType string) (map[string]string, error) {
	if contentType == "" {
		return nil, errors.New("the value of contentType is empty")
	}

	tokens := strings.Split(contentType, ";")

	if len(tokens) > contentTypeMaxLen {
		return nil, errors.New("the value of contentType is out of rang")
	}

	m := make(map[string]string, common.DefaultMapSize)
	for _, token := range tokens {
		token = strings.TrimSpace(token)
		if strings.HasPrefix(token, "application") {
			continue
		}
		kv := strings.SplitN(token, "=", tokenSplit)
		if len(kv) != tokenSplit {
			continue // ignore
		}
		m[kv[0]] = kv[1]
	}
	return m, nil
}
