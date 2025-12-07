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
// package main start of meta-service
package main

import (
	"flag"
	"fmt"
	"os"

	"meta_service/common/logger/log"
	"meta_service/common/signals"
	"meta_service/common/versions"
	"meta_service/etcd"
	"meta_service/function_repo/config"
	"meta_service/function_repo/storage"
	"meta_service/router"
)

const (
	defaultListenIP = "0.0.0.0"
	defaultFileName = "meta-service"
)

func initLogConfig(logFile string) error {
	if err := log.InitRunLogByParam(logFile, defaultFileName); err != nil {
		fmt.Printf("failed to init run logger, logConfigPath: %s, error: %s\n", logFile, err.Error())
		return err
	}
	return nil
}

func initRouterStorage() error {
	etcdclient, err := etcd.NewClient()
	if err != nil {
		fmt.Printf("failed to new etcd client, error: %s", err.Error())
		return err
	}

	err = storage.InitStorage(etcdclient)
	if err != nil {
		fmt.Printf("failed to init storage, error: %s", err.Error())
		return err
	}
	return nil
}

func initMetaStorage() error {
	if !config.RepoCfg.MetaEtcdEnable {
		return nil
	}
	metaEtcdClient, err := etcd.NewMetaClient()
	if err != nil {
		log.GetLogger().Errorf("failed to new mete etcd client, error: %s", err.Error())
		return err
	}

	err = storage.InitMetaStorage(metaEtcdClient)
	if err != nil {
		log.GetLogger().Errorf("failed to init storage, error: %s", err.Error())
		return err
	}
	return nil
}

func initStorage() error {
	err := initRouterStorage()
	if err != nil {
		fmt.Printf("failed to init router storage, error: %s", err.Error())
		return err
	}
	err = initMetaStorage()
	if err != nil {
		fmt.Printf("failed to init meta storage, error: %s", err.Error())
		return err
	}
	return nil
}

func initMetaService(metaServiceConfigFile, logConfigFile string) bool {
	if err := initLogConfig(logConfigFile); err != nil {
		return false
	}
	if err := config.InitConfig(metaServiceConfigFile); err != nil {
		fmt.Printf("failed to initialize config, error:%s\n", err.Error())
		return false
	}
	if err := initStorage(); err != nil {
		fmt.Printf("failed to initialize storage, error:%s\n", err.Error())
		return false
	}
	return true
}

func run() {
	stopCh := signals.WaitForSignal()
	fmt.Printf("version: %s branch: %s commit_id: %s\n", versions.GetBuildVersion(), versions.GetGitBranch(),
		versions.GetGitHash())
	ip := config.RepoCfg.ServerCfg.IP
	if len(ip) == 0 {
		ip = os.Getenv("POD_IP")
		fmt.Printf("failed to get pod ip from config, try to use %s as listen IP", ip)
	}
	if len(ip) == 0 {
		ip = defaultListenIP
		fmt.Printf("failed to get pod ip from env POD_IP, try to use %s as listen IP", ip)
	}
	addr := fmt.Sprintf("%s:%d", ip, config.RepoCfg.ServerCfg.Port)
	fmt.Printf("starting router at %s ...\n", addr)

	router.Run(addr, stopCh)
}

func main() {
	fmt.Print("starting...\n")
	configFile := flag.String("config_path", "/home/yuanrong/config/config.json", "configuration path")
	logConfigFile := flag.String("log_config_path", "/home/yuanrong/config/log.json", "log configuration path")
	flag.Parse()

	if !initMetaService(*configFile, *logConfigFile) {
		return
	}

	run()
}
