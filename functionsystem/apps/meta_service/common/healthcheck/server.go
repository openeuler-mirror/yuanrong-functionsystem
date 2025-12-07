/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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

// Package healthcheck implements a common health check server
package healthcheck

import (
	"crypto/tls"
	"net/http"

	"meta_service/common/logger/log"

	"github.com/gin-gonic/gin"

	commontls "meta_service/common/tls"
)

const port = "8090"

// k8s https livenessProbe

// StartServe start health check server for k8s
func StartServe(ip string, f func(c *gin.Context)) error {
	engine := gin.New()
	engine.Use(gin.Recovery())

	checkFunc := healthCheck
	if f != nil {
		checkFunc = f
	}

	engine.GET("/healthz", checkFunc)

	server := http.Server{
		Addr:    ip + ":" + port,
		Handler: engine,
	}

	err := server.ListenAndServe()
	if err != nil {
		log.GetLogger().Errorf("failed to start health check server %s", err.Error())
		return err
	}

	log.GetLogger().Infof("start health check server at %s", ip)
	return nil
}

// StartServeTLS start tls health check server for k8s
func StartServeTLS(ip, certFile, keyFile, passPhase string, f func(c *gin.Context)) error {
	engine := gin.New()
	engine.Use(gin.Recovery())

	checkFunc := healthCheck
	if f != nil {
		checkFunc = f
	}
	engine.GET("/healthz", checkFunc)

	healthConf := commontls.NewTLSConfig(commontls.WithClientAuthType(tls.NoClientCert),
		commontls.WithCertsByEncryptedKey(certFile, keyFile, passPhase))

	server := http.Server{
		Addr:      ip + ":" + port,
		Handler:   engine,
		TLSConfig: healthConf,
	}

	err := server.ListenAndServeTLS("", "")
	if err != nil {
		log.GetLogger().Errorf("failed to start health check server %s", err.Error())
		return err
	}

	log.GetLogger().Infof("start health check tls server at %s", ip)
	return nil
}

func healthCheck(c *gin.Context) {
	return
}
