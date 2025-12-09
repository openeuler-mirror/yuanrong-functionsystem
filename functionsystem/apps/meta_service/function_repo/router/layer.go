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
	"net/http"
	"strconv"
	"time"

	"meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils"
)

const (
	minVersionNum     = 1
	maxVersionNum     = 1000000
	maxCompatibleNum  = 64
	retryTimes        = 5
	retryPeriodicTime = 500 * time.Millisecond
)

func regLayerHandlers(r server.RouterGroup) {
	// /:layerName/versions -> createLayer
	// /update/:layerName -> updateLayer
	r.POST("/:v1/:v2", createOrUpdateLayer)

	r.DELETE("/:layerName", deleteLayer)
	r.DELETE("/:layerName/versions/:versionNumber", deleteLayerVersion)
	r.GET("", getLayerList)
	r.GET("/:layerName/versions/:versionNumber", getLayerVersion)
	r.GET("/:layerName/versions", getLayerVersionList)
}

func createOrUpdateLayer(c server.Context) {
	v1 := c.Gin().Param("v1")
	v2 := c.Gin().Param("v2")

	if v1 == "update" {
		updateLayer(c, v2)
	} else if v2 == "versions" {
		createLayer(c, v1)
	} else {
		c.Gin().AbortWithStatus(http.StatusNotFound)
	}
}

func createLayer(c server.Context, lname string) {
	compatibleRuntimes := c.Gin().Request.Header.Get(constants.HeaderCompatibleRuntimes)
	description := c.Gin().Request.Header.Get(constants.HeaderDescription)
	licenseInfo := c.Gin().Request.Header.Get(constants.HeaderLicenseInfo)
	if lname == "" || len(lname) > paramMaxLength {
		log.GetLogger().Errorf("invalid layerName length")
		utils.BadRequest(c, errors.New("invalid layerName length"))
		return
	}

	log.GetLogger().Infof("create layer: %s", lname)
	info, err := service.ParseLayerInfo(c, lname)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer query info: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

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
	req := model.CreateLayerRequest{
		ZipFileSize:        fileSize,
		CompatibleRuntimes: compatibleRuntimes,
		Description:        description,
		LicenseInfo:        licenseInfo,
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.LayerName,
		Qualifier:    "",
	})

	var resp model.Layer
	for i := 0; i < retryTimes; i++ {
		resp, err = service.CreateLayer(c, info.LayerName, req)
		if err == errmsg.EtcdTransactionFailedError {
			log.GetLogger().Errorf("failed to create %s layer: %s, retry create", info.LayerName, err.Error())
			time.Sleep(time.Duration(i+1) * retryPeriodicTime)
			continue
		}
		break
	}
	utils.WriteResponse(c, resp, err)
}

func updateLayer(c server.Context, lname string) {
	if lname == "" || len(lname) > paramMaxLength {
		log.GetLogger().Errorf("invalid layerName length")
		utils.BadRequest(c, errors.New("invalid layerName length"))
		return
	}

	log.GetLogger().Infof("update layer: %s", lname)
	info, err := service.ParseLayerInfo(c, lname)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer query info: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	var req model.UpdateLayerRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind json request: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: lname,
		Qualifier:    "",
	})

	var resp model.Layer
	for i := 0; i < retryTimes; i++ {
		resp, err = service.UpdateLayer(c, info.LayerName, req)
		if err == errmsg.EtcdTransactionFailedError {
			log.GetLogger().Errorf("failed to update %s layer: %s, retry update", info.LayerName, err.Error())
			time.Sleep(time.Duration(i+1) * retryPeriodicTime)
			continue
		}
		break
	}
	utils.WriteResponse(c, resp, err)
}

func getLayerQueryInfo(c server.Context) (model.LayerQueryInfo, bool) {
	lname := c.Gin().Param("layerName")
	if lname == "" || len(lname) > paramMaxLength {
		log.GetLogger().Errorf("invalid layerName length")
		utils.BadRequest(c, errors.New("invalid layerName length"))
		return model.LayerQueryInfo{}, true
	}
	info, err := service.ParseLayerInfo(c, lname)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer query info: %s", err.Error())
		utils.BadRequest(c, err)
		return model.LayerQueryInfo{}, true
	}
	return info, false
}

func deleteLayer(c server.Context) {
	info, done := getLayerQueryInfo(c)
	if done {
		return
	}

	log.GetLogger().Infof("delete layer: %s", info.LayerName)
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.LayerName,
		Qualifier:    "",
	})

	var err error
	for i := 0; i < retryTimes; i++ {
		err = service.DeleteLayer(c, info.LayerName)
		if err == errmsg.EtcdTransactionFailedError {
			log.GetLogger().Errorf("failed to delete %s layer: %s, retry delete", info.LayerName, err.Error())
			time.Sleep(time.Duration(i+1) * retryPeriodicTime)
			continue
		}
		break
	}
	utils.WriteResponse(c, nil, err)
}

func getLayerNameAndVersion(c server.Context) (string, int, bool) {
	info, done := getLayerQueryInfo(c)
	if done {
		return "", 0, true
	}
	vstr := c.Gin().Param("versionNumber")
	version, err := strconv.Atoi(vstr)
	if err != nil {
		log.GetLogger().Errorf("failed to parse version number: %s", err)
		utils.BadRequest(c, err)
		return "", 0, true
	}
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.LayerName,
		Qualifier:    vstr,
	})
	return info.LayerName, version, false
}

func deleteLayerVersion(c server.Context) {
	layerName, version, done := getLayerNameAndVersion(c)
	if done {
		return
	}

	log.GetLogger().Infof("delete layer version: %s, %d", layerName, version)
	if version < minVersionNum || version > maxVersionNum {
		log.GetLogger().Errorf("invalid versionNumber")
		utils.BadRequest(c, errors.New("invalid versionNumber"))
		return
	}

	var err error
	for i := 0; i < retryTimes; i++ {
		err = service.DeleteLayerVersion(c, layerName, version)
		if err == errmsg.EtcdTransactionFailedError {
			log.GetLogger().Infof("failed to delete %s layer version: %s, retry delete", layerName, err.Error())
			time.Sleep(time.Duration(i+1) * retryPeriodicTime)
			continue
		}
		break
	}
	utils.WriteResponse(c, nil, err)
}

func getLayerList(c server.Context) {
	var req model.GetLayerListRequest
	if err := utils.ShouldBindQuery(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind json request: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	log.GetLogger().Infof("get layer list: %s, %d", req.LayerName, req.LayerVersion)
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: req.LayerName,
		Qualifier:    strconv.Itoa(req.LayerVersion),
	})
	resp, err := service.GetLayerList(c, req)
	utils.WriteResponse(c, resp, err)
}

func getLayerVersion(c server.Context) {
	layerName, version, done := getLayerNameAndVersion(c)
	if done {
		return
	}
	log.GetLogger().Infof("get layer version: %s, %d", layerName, version)

	// we allow version 0 as the "latest" version.
	if version < 0 || version > maxVersionNum {
		log.GetLogger().Errorf("invalid versionNumber")
		utils.BadRequest(c, errors.New("invalid versionNumber"))
		return
	}
	resp, err := service.GetLayerVersion(c, layerName, version)
	utils.WriteResponse(c, resp, err)
}

func getLayerVersionList(c server.Context) {
	lname := c.Gin().Param("layerName")
	if lname == "" || len(lname) > paramMaxLength {
		log.GetLogger().Errorf("invalid layerName length")
		utils.BadRequest(c, errors.New("invalid layerName length"))
		return
	}

	log.GetLogger().Infof("get layer version list: %s", lname)
	runtime := c.Gin().Query("compatibleRuntime")
	if len(runtime) > maxCompatibleNum {
		log.GetLogger().Errorf("invalid compatibleRuntime length")
		utils.BadRequest(c, errors.New("invalid compatibleRuntime length"))
		return
	}

	idx, size, err := utils.QueryPaging(c)
	if err != nil {
		utils.BadRequest(c, err)
		return
	}

	info, err := service.ParseLayerInfo(c, lname)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer query info: %s", err.Error())
		utils.BadRequest(c, err)
		return
	}

	req := model.GetLayerVersionListRequest{
		LayerName:         info.LayerName,
		CompatibleRuntime: runtime,
		PageIndex:         idx,
		PageSize:          size,
	}
	c.InitResourceInfo(server.ResourceInfo{
		ResourceName: info.LayerName,
		Qualifier:    "",
	})
	resp, err := service.GetLayerVersionList(c, req)
	utils.WriteResponse(c, resp, err)
}
