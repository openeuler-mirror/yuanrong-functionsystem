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

// Package fakecontext contains a fake context that should only be used in tests
package fakecontext

import (
	"context"

	"github.com/gin-gonic/gin"

	"meta_service/function_repo/server"
)

// Context defines a fake server context for tests to easily mock http requests
type Context struct {
	tenantInfo   *server.TenantInfo
	resourceInfo *server.ResourceInfo
	ctx          context.Context
	ginCtx       *gin.Context
}

// NewContext init a context with context.Background
func NewContext() server.Context {
	return NewContextWithContext(context.Background())
}

// NewMockContext init a context with fixed value
func NewMockContext() server.Context {
	mockContext := NewContext()
	info := server.TenantInfo{
		BusinessID: "yrk",
		TenantID:   "i1fe539427b24702acc11fbb4e134e17",
		ProductID:  "",
	}
	mockContext.InitTenantInfo(info)
	resourceInfo := server.ResourceInfo{
		ResourceName: "TestResource",
		Qualifier:    "ResourceVersion",
	}
	mockContext.InitResourceInfo(resourceInfo)
	return mockContext
}

// NewContextWithContext init a context
func NewContextWithContext(ctx context.Context) server.Context {
	return &Context{
		ctx:    ctx,
		ginCtx: new(gin.Context),
	}
}

// Gin implements server.Context
func (c *Context) Gin() *gin.Context {
	return c.ginCtx
}

// Context implements server.Context
func (c *Context) Context() context.Context {
	return c.ctx
}

// InitTenantInfo implements server.Context
func (c *Context) InitTenantInfo(info server.TenantInfo) {
	c.tenantInfo = &info
}

// TenantInfo implements server.Context
func (c *Context) TenantInfo() (server.TenantInfo, error) {
	if c.tenantInfo != nil {
		return *c.tenantInfo, nil
	}
	return server.TenantInfo{}, server.ErrNoTenantInfo
}

// InitResourceInfo implements server.Context
func (c *Context) InitResourceInfo(info server.ResourceInfo) {
	c.resourceInfo = &info
}

// ResourceInfo implements server.Context
func (c *Context) ResourceInfo() server.ResourceInfo {
	if c.resourceInfo != nil {
		return *c.resourceInfo
	}
	return server.ResourceInfo{}
}
