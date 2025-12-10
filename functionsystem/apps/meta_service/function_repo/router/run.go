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

// Package router implements an api server with routers registered
package router

import (
	"net/http"

	"github.com/gin-gonic/gin"

	"meta_service/common/utils"
	"meta_service/function_repo/server"
)

const (
	paramMaxLength int = 256
)

// Run starts the server
func Run(addr string, stopCh <-chan struct{}) {
	r := RegHandlers()
	if err := r.Run(addr, stopCh); err != nil {
		utils.ProcessBindErrorAndExit(err)
	}
}

// RegHandlers registering route
func RegHandlers() server.Engine {
	gin.SetMode(gin.ReleaseMode)
	r := server.New()
	rg := r.Group("/function-repository/v1")
	rg.Use(LogMiddle())
	rg.Use(InitTenantInfoMiddle())
	regFunctionHandlers(rg.Group("functions"))
	regPublishHandlers(rg.Group("functions"))
	regAliasHandler(rg.Group("functions"))
	regLayerHandlers(rg.Group("layers"))
	regTriggerHandlers(rg.Group("triggers"))
	regServiceHandlers(rg.Group("snService"))
	regPodPoolHandlers(rg.Group("podpools"))
	r.Group("/healthz").GET("", func(c server.Context) {
		c.Gin().Data(http.StatusOK, "", nil)
	})
	return r
}
