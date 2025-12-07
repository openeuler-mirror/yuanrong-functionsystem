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
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/service"
	"meta_service/function_repo/utils"
)

const (
	maxTriggerLen    = 64
	maxFunctionIDLen = 255
)

func regTriggerHandlers(r server.RouterGroup) {
	r.POST("", createTrigger)
	r.DELETE("/:triggerId", deleteTrigger)
	r.DELETE("/:triggerId/:functionId", deleteTriggers)
	r.GET("/:triggerId", getTrigger)
	r.GET("", getTriggerList)
	r.PUT("/:triggerId", updateTrigger)
}

func checkTriggerID(tid string) error {
	if tid == "" || len(tid) > maxTriggerLen {
		log.GetLogger().Errorf("invalid triggerid length: %d", len(tid))
		return errmsg.NewParamError("invalid triggerid length: %d", len(tid))
	}
	return nil
}

func checkFunctionID(fid string) error {
	if fid == "" || len(fid) > maxFunctionIDLen {
		log.GetLogger().Errorf("invalid functionid length: %d", len(fid))
		return errmsg.NewParamError("invalid functionid length: %d", len(fid))
	}
	return nil
}

func createTrigger(c server.Context) {
	var req model.TriggerCreateRequest
	err := shouldBindJSON(c, &req)
	if err != nil {
		log.GetLogger().Errorf("invalid param in request")
		utils.BadRequest(c, err)
		return
	}
	resp, err := service.CreateTrigger(c, req)
	utils.WriteResponse(c, resp, err)
}

func deleteTrigger(c server.Context) {
	tid := c.Gin().Param("triggerId")
	if tid == "function" {
		deleteTriggers(c)
		return
	}

	if err := checkTriggerID(tid); err != nil {
		utils.BadRequest(c, errmsg.NewParamError("invalid triggerid length: %d", len(tid)))
		return
	}
	err := service.DeleteTriggerByID(c, tid)
	utils.WriteResponse(c, nil, err)
}

func deleteTriggers(c server.Context) {
	tid := c.Gin().Param("triggerId")
	if tid != "function" {
		log.GetLogger().Errorf("invalid param in request")
		utils.BadRequest(c, errors.New("invalid param in request"))
		return
	}
	fid := c.Gin().Param("functionId")

	if err := checkFunctionID(fid); err != nil {
		utils.BadRequest(c, errmsg.NewParamError("invalid functionid length: %d", len(fid)))
		return
	}

	err := service.DeleteTriggerByFuncID(c, fid)
	utils.WriteResponse(c, nil, err)
}

func getTrigger(c server.Context) {
	tid := c.Gin().Param("triggerId")
	if err := checkTriggerID(tid); err != nil {
		utils.BadRequest(c, errmsg.NewParamError("invalid triggerid length: %d", len(tid)))
		return
	}
	resp, err := service.GetTrigger(c, tid)
	utils.WriteResponse(c, resp, err)
}

func getTriggerList(c server.Context) {
	idx, size, err := utils.QueryPaging(c)
	if err != nil {
		log.GetLogger().Errorf("invalid page info")
		utils.BadRequest(c, err)
		return
	}

	fid := c.Gin().Query("funcId")
	if err := checkFunctionID(fid); err != nil {
		utils.BadRequest(c, errmsg.NewParamError("invalid functionid length: %d", len(fid)))
		return
	}
	resp, err := service.GetTriggerList(c, idx, size, fid)
	utils.WriteResponse(c, resp, err)
}

func updateTrigger(c server.Context) {
	var req model.TriggerUpdateRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("invalid param in request")
		utils.BadRequest(c, err)
		return
	}

	tid := c.Gin().Param("triggerId")
	if err := checkTriggerID(tid); err != nil {
		utils.BadRequest(c, errmsg.NewParamError("invalid triggerid length: %d", len(tid)))
		return
	}
	req.TriggerID = tid
	resp, err := service.UpdateTrigger(c, req)
	utils.WriteResponse(c, resp, err)
}
