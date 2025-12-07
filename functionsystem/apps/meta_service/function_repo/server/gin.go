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

package server

import (
	"context"
	"net/http"
	"os"
	"time"

	"github.com/gin-gonic/gin"

	"meta_service/common/healthcheck"
	"meta_service/common/logger/log"
	"meta_service/common/reader"
	commontls "meta_service/common/tls"
	"meta_service/function_repo/config"
)

const (
	graceExitTime time.Duration = 30
)

type engine struct {
	engine *gin.Engine
	srv    *http.Server
}

// New returns a new http server engine powered by gin
func New() Engine {
	return engine{
		engine: gin.New(),
	}
}

// Group implements Engine
func (e engine) Group(relativePath string, handlers ...HandlerFunc) RouterGroup {
	return newRouterGroup(e.engine.Group(relativePath, makeHandlersChain(handlers)...))
}

// Run implements Engine
func (e engine) Run(addr string, stopCh <-chan struct{}) error {
	e.srv = &http.Server{
		Addr:    addr,
		Handler: e.engine,
	}
	go func() {
		if stopCh != nil {
			<-stopCh
		}
		log.GetLogger().Warnf("received termination signal to shutdown")
		e.GraceExit()
	}()
	var passPhase []byte
	var err error
	if config.RepoCfg.MutualTLSConfig.TLSEnable && config.RepoCfg.MutualTLSConfig.PwdFile != "" {
		passPhase, err = reader.ReadFileWithTimeout(config.RepoCfg.MutualTLSConfig.PwdFile)
		if err != nil {
			log.GetLogger().Errorf("failed to read file PwdFile: %s", err.Error())
			return err
		}
	}

	if config.RepoCfg.MutualTLSConfig.TLSEnable {
		ip := config.RepoCfg.ServerCfg.IP
		if len(ip) == 0 {
			ip = os.Getenv("POD_IP")
		}
		go healthcheck.StartServeTLS(ip, config.RepoCfg.MutualTLSConfig.ModuleCertFile,
			config.RepoCfg.MutualTLSConfig.ModuleKeyFile, string(passPhase), nil)
	}
	if config.RepoCfg.MutualTLSConfig.TLSEnable {
		e.srv.TLSConfig = commontls.NewTLSConfig(
			commontls.BuildServerTLSConfOpts(config.RepoCfg.MutualTLSConfig)...)
		err = e.srv.ListenAndServeTLS("", "")
	} else {
		err = e.srv.ListenAndServe()
	}
	if err != nil {
		log.GetLogger().Errorf("failed to serve, err: %s", err.Error())
		return err
	}
	return nil
}

// GraceExit graceful exit when receive a SIGTERM signal
func (e engine) GraceExit() {
	if e.srv == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), graceExitTime*time.Second)
	defer cancel()
	if err := e.srv.Shutdown(ctx); err != nil {
		log.GetLogger().Errorf("failed to shutdown repository: %s", err.Error())
	}
	log.GetLogger().Info("graceful exit")
}

type routerGroup struct {
	rg *gin.RouterGroup
}

func newRouterGroup(rg *gin.RouterGroup) RouterGroup {
	return routerGroup{rg}
}

// GET implements RouterGroup
func (rg routerGroup) GET(relativePath string, handlers ...HandlerFunc) {
	rg.rg.GET(relativePath, makeHandlersChain(handlers)...)
}

// POST implements RouterGroup
func (rg routerGroup) POST(relativePath string, handlers ...HandlerFunc) {
	rg.rg.POST(relativePath, makeHandlersChain(handlers)...)
}

// DELETE implements RouterGroup
func (rg routerGroup) DELETE(relativePath string, handlers ...HandlerFunc) {
	rg.rg.DELETE(relativePath, makeHandlersChain(handlers)...)
}

// PUT implements RouterGroup
func (rg routerGroup) PUT(relativePath string, handlers ...HandlerFunc) {
	rg.rg.PUT(relativePath, makeHandlersChain(handlers)...)
}

// Group implements RouterGroup
func (rg routerGroup) Group(relativePath string, handlers ...HandlerFunc) RouterGroup {
	return routerGroup{
		rg.rg.Group(relativePath, makeHandlersChain(handlers)...),
	}
}

// Use implements RouterGroup
func (rg routerGroup) Use(middleware ...HandlerFunc) {
	rg.rg.Use(makeHandlersChain(middleware)...)
}

type ctx struct {
	ginCtx *gin.Context
}

// NewContext -
func NewContext(c *gin.Context) Context {
	return ctx{c}
}

// Gin implements Context
func (c ctx) Gin() *gin.Context {
	return c.ginCtx
}

// Context implements Context
func (c ctx) Context() context.Context {
	return c.Gin().Request.Context()
}

// InitTenantInfo implements Context
func (c ctx) InitTenantInfo(info TenantInfo) {
	c.ginCtx.Set("tenant", info)
}

// TenantInfo implements Context
func (c ctx) TenantInfo() (TenantInfo, error) {
	raw, exist := c.ginCtx.Get("tenant")
	if !exist {
		return TenantInfo{}, ErrNoTenantInfo
	}
	info, ok := raw.(TenantInfo)

	var err error
	if !ok {
		err = ErrNoTenantInfo
	}
	return info, err
}

// InitResourceInfo implements Context
func (c ctx) InitResourceInfo(info ResourceInfo) {
	c.ginCtx.Set("resource", info)
}

// ResourceInfo implements Context
func (c ctx) ResourceInfo() ResourceInfo {
	raw, exist := c.ginCtx.Get("resource")
	if !exist {
		return ResourceInfo{}
	}
	info, ok := raw.(ResourceInfo)

	if !ok {
		info = ResourceInfo{}
	}
	return info
}

func makeHandlersChain(handlers []HandlerFunc) []gin.HandlerFunc {
	chain := make([]gin.HandlerFunc, 0, len(handlers))
	for _, handler := range handlers {
		chain = append(chain, func(c *gin.Context) {
			handler(NewContext(c))
		})
	}
	return chain
}

// ServeHTTP A Handler responds to an HTTP request
func (e engine) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	e.engine.ServeHTTP(w, req)
}
