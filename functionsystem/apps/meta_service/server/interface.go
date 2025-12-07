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

// Package server wraps useful methods creating a http server for meta service
package server

import (
	"context"
	"net/http"

	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"

	"meta_service/common/snerror"

	"github.com/gin-gonic/gin"
)

// ErrNoTenantInfo means tenant info does not exist.
var ErrNoTenantInfo = snerror.New(errmsg.NoTenantInfo, "tenantInfo does not exist")

// TenantInfo has tenant information including business ID, tenant ID and product ID.
type TenantInfo struct {
	BusinessID, TenantID, ProductID string
}

// ResourceInfo includes basic information useful for logging.
type ResourceInfo struct {
	ResourceName, Qualifier, FunctionVersionNumber string
	FunctionType                                   config.FunctionType
}

// Engine defines a http server engine
type Engine interface {
	Group(relativePath string, handlers ...HandlerFunc) RouterGroup
	Run(addr string) error
	GraceExit()
	ServeHTTP(w http.ResponseWriter, req *http.Request)
}

// RouterGroup is used internally to configure router, a RouterGroup is associated with a prefix and an array of
// handlers (middleware).
type RouterGroup interface {
	GET(relativePath string, handlers ...HandlerFunc)
	POST(relativePath string, handlers ...HandlerFunc)
	DELETE(relativePath string, handlers ...HandlerFunc)
	PUT(relativePath string, handlers ...HandlerFunc)
	Group(relativePath string, handlers ...HandlerFunc) RouterGroup
	Use(handlers ...HandlerFunc)
}

// HandlerFunc defines the handler used by middleware as return value.
type HandlerFunc func(Context)

// Context defines information whose life cycle is within a http request
type Context interface {
	Gin() *gin.Context
	Context() context.Context
	InitTenantInfo(TenantInfo)
	TenantInfo() (TenantInfo, error)
	InitResourceInfo(ResourceInfo)
	ResourceInfo() ResourceInfo
}
