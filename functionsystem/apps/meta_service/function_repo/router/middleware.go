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

// Package router
package router

import (
	"strconv"
	"strings"
	"time"

	"meta_service/common/logger/log"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils"
)

const (
	xBusinessID           = "x-business-id"
	xTenantID             = "x-tenant-id"
	xProductID            = "x-product-id"
	maxContentTypeLength  = 100
	uploadCodeContentType = "application/vnd.yuanrong+attachment"
	uploadCodeFileSize    = "file-size"
	uploadCodeRevisionID  = "revision-id"
)

const (
	defaultUserID    = "12345678901234561234567890123456"
	defaultTenantID  = "12345678901234561234567890123456"
	defaultPrivilege = "1111"
	defaultAppID     = "yrk"
	defaultAppSecret = "12CFV18835434FDGEEF39BD6YRE45D46"
	// HeaderBusinessID -
	HeaderBusinessID = "X-Business-ID"
	// HeaderTenantID -
	HeaderTenantID = "X-Tenant-ID"
	// HeaderPrivilege is Privilege id
	HeaderPrivilege = "X-Privilege"
	// HeaderUserID is User ID
	HeaderUserID = "X-User-Id"
	// AppIDKey is context key of appID
	AppIDKey = "appID"
	// AppSecretKey is context key of appSecret
	AppSecretKey = "appSecret"
)

// SetRequestHead adding a Tenant ID to a Request Header
func SetRequestHead() server.HandlerFunc {
	return func(c server.Context) {
		c.Gin().Request.Header.Set(HeaderUserID, defaultUserID)

		if c.Gin().Request.Header.Get(HeaderTenantID) == "" {
			c.Gin().Request.Header.Set(HeaderTenantID, defaultTenantID)
		}

		c.Gin().Request.Header.Set(HeaderPrivilege, defaultPrivilege)
		c.Gin().Request.Header.Set(HeaderBusinessID, defaultAppID)

		c.Gin().Set(AppIDKey, defaultAppID)
		c.Gin().Set(AppSecretKey, defaultAppSecret)

		c.Gin().Next()
	}
}

// LogMiddle middle of log
func LogMiddle() server.HandlerFunc {
	return func(c server.Context) {
		start := time.Now()
		path := c.Gin().Request.URL.Path
		query := c.Gin().Request.URL.RawQuery
		c.Gin().Next()
		latency := time.Since(start)
		if len(c.Gin().Errors) > 0 {
			for _, e := range c.Gin().Errors.Errors() {
				log.GetLogger().Error(e)
			}
		}
		header := c.Gin().Request.Header
		write(interfaceLog{
			httpMethod:  c.Gin().Request.Method,
			ip:          c.Gin().ClientIP(),
			requestPath: path,
			query:       query,
			bussinessID: header.Get("x-business-id"),
			traceID:     header.Get("x-trace-id"),
			retCode:     strconv.Itoa(c.Gin().Writer.Status()),
			costTime:    latency.String(),
		})
	}
}

func isUploadCode(contentType string) (bool, string) {
	if len(contentType) > maxContentTypeLength {
		return false, ""
	}

	ct := strings.ReplaceAll(contentType, " ", "")
	fields := strings.Split(ct, ";")

	result := false
	info := ""
	for _, f := range fields {
		if f == uploadCodeContentType {
			result = true
		} else if strings.HasPrefix(f, uploadCodeFileSize) {
			info = f
		} else if strings.HasPrefix(f, uploadCodeRevisionID) {
			info = info + "&" + f
		}
	}
	return result, info
}

// InitTenantInfoMiddle: Init Tenant Info
func InitTenantInfoMiddle() server.HandlerFunc {
	return func(c server.Context) {
		header := c.Gin().Request.Header
		info := server.TenantInfo{
			BusinessID: header.Get(xBusinessID),
			TenantID:   header.Get(xTenantID),
			ProductID:  header.Get(xProductID),
		}
		if err := utils.Validate(info); err != nil {
			utils.BadRequest(c, err)
			c.Gin().Abort()
		} else {
			c.InitTenantInfo(info)
			c.Gin().Next()
		}
	}
}
