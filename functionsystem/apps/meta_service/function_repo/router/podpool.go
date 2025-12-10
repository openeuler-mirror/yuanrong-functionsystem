/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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
)

func regPodPoolHandlers(r server.RouterGroup) {
	r.POST("", createPodPool)
	r.DELETE("", deletePodPool)
	r.PUT("/:id", updatePodPool)
	r.GET("", getPodPool)
}

func createPodPool(c server.Context) {
	var req model.PodPoolCreateRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind json :%s", err)
		utils.BadRequest(c, err)
		return
	}
	log.GetLogger().Debugf("start to create pod pools, pod pool num: %d", len(req.Pools))
	resp, err := service.CreatePodPool(c, req)
	utils.WriteResponseWithMsg(c, resp, err)
}

func deletePodPool(c server.Context) {
	id := c.Gin().Query("id")
	group := c.Gin().Query("group")
	log.GetLogger().Debugf("start to delete pod pool, pod pool id: %s, group: %s", id, group)
	err := service.DeletePodPool(c, id, group)
	utils.WriteResponse(c, nil, err)
}

func updatePodPool(c server.Context) {
	var req model.PodPoolUpdateRequest
	if err := shouldBindJSON(c, &req); err != nil {
		log.GetLogger().Errorf("failed to bind json :%s", err)
		utils.BadRequest(c, err)
		return
	}
	req.ID = c.Gin().Param("id")
	log.GetLogger().Debugf("start to update pod pool, id： %s, size: %d", req.ID, req.Size)
	err := service.UpdatePodPool(c, req)
	utils.WriteResponse(c, nil, err)
}

func getPodPool(c server.Context) {
	id := c.Gin().Query("id")
	group := c.Gin().Query("group")
	limit := c.Gin().Query("limit")
	limitInt, err := strconv.Atoi(limit)
	if err != nil {
		log.GetLogger().Errorf("failed to convert string: %s to int, err: %s", limit, err)
		utils.BadRequest(c, err)
		return
	}
	offset := c.Gin().Query("offset")
	offsetInt, err := strconv.Atoi(offset)
	if err != nil {
		log.GetLogger().Errorf("failed to convert string: %s to int, err: %s", limit, err)
		utils.BadRequest(c, err)
		return
	}
	req := model.PodPoolGetRequest{
		ID:     id,
		Group:  group,
		Limit:  limitInt,
		Offset: offsetInt,
	}
	log.GetLogger().Debugf("start to get pod pool, id： %s, group: %s, limit: %d, offset: %d",
		req.ID, req.Group, req.Limit, req.Offset)
	resp, err := service.GetPodPool(c, req)
	utils.WriteResponse(c, resp, err)
}
