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

// Package router implements an api server with routers registered
package router

import (
	"net/http"

	"meta_service/function_repo/router"
	"meta_service/function_repo/server"

	"meta_service/common/utils"

	"github.com/gin-gonic/gin"
)

// Run starts the server
func Run(addr string, stopCh <-chan struct{}) {
	gin.SetMode(gin.ReleaseMode)
	r := server.New()
	rg := r.Group("/serverless/v1") // 路由是/serverless/v1
	rg.Use(router.LogMiddle())
	rg.Use(router.SetRequestHead())
	rg.Use(router.InitTenantInfoMiddle())
	regFunctionHandlers(rg.Group("functions"))
	regPodPoolHandlers(rg.Group("podpools"))
	r.Group("/healthz").GET("", func(c server.Context) {
		c.Gin().Data(http.StatusOK, "", nil)
	})
	if err := r.Run(addr, stopCh); err != nil {
		utils.ProcessBindErrorAndExit(err)
	}
}
