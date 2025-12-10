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

// Package status is start cmd
package status

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"os"
	"runtime"
	"time"

	"github.com/spf13/cobra"
	clientv3 "go.etcd.io/etcd/client/v3"

	"cli/constant"
	"cli/pkg/cmdio"
	"cli/utils"
	"cli/utils/colorprint"
)

type yrStatusOptions struct {
	cmdIO              *cmdio.CmdIO
	etcdEndpoint       string
	etcdTlsEnable      bool
	etcdCaFile         string
	etcdClientKeyFile  string
	etcdClientCertFile string
}

var yrOpts yrStatusOptions

var yrStatusCmd = &cobra.Command{
	Use:   "status",
	Short: fmt.Sprintf("status %s Platform", constant.PlatformName),
	Long:  fmt.Sprintf(`status %s Platform`, constant.PlatformName),
	Example: utils.RawFormat(fmt.Sprintf(`
    %s status in non-interactive mode:
    $ %s status
	`, constant.CliName, constant.CliName)),
	Args: utils.NoArgs,
	RunE: yrStatusYuanRong,
}

// InitYrCMD init login cmd
func InitYrCMD(cio *cmdio.CmdIO) *cobra.Command {
	yrOpts.cmdIO = cio
	// 添加命令行参数
	yrStatusCmd.Flags().StringVar(&yrOpts.etcdEndpoint, "etcd_endpoint", "", "ETCD endpoint address")
	yrStatusCmd.Flags().BoolVar(&yrOpts.etcdTlsEnable, "etcd_tls_enable", false, "Enable ETCD TLS")
	yrStatusCmd.Flags().StringVar(&yrOpts.etcdCaFile, "etcd_ca_file", "", "CA certificate for ETCD service")
	yrStatusCmd.Flags().StringVar(&yrOpts.etcdClientKeyFile, "etcd_client_key_file", "", "Client cert for ETCD")
	yrStatusCmd.Flags().StringVar(&yrOpts.etcdClientCertFile, "etcd_client_cert_file", "", "Client key for ETCD")
	yrStatusCmd.MarkFlagsRequiredTogether("etcd_tls_enable",
		"etcd_ca_file", "etcd_client_key_file", "etcd_client_cert_file")
	return yrStatusCmd
}

func yrStatusYuanRong(cmd *cobra.Command, args []string) error {
	if !utils.SupportSystem() {
		return errors.New(fmt.Sprintf("unsupported os: %s", runtime.GOOS))
	}

	info, err := utils.GetMasterInfoFromFile(constant.YuanRongCurrentMasterInfoPath)
	if err != nil || info == nil {
		colorprint.PrintFail(yrOpts.cmdIO.Out, "there seems no running yuanrong cluster.\n", err.Error(), ".\n")
		return nil
	}
	if yrOpts.etcdEndpoint == "" {
		yrOpts.etcdEndpoint = fmt.Sprintf("%s:%s", info.EtcdIP, info.EtcdPort)
	}

	readyAgentCount, err := countAgent()
	if err != nil {
		return err
	} else {
		colorprint.PrintSuccess(yrOpts.cmdIO.Out, "YuanRong is running normally,", fmt.Sprintf(`
YuanRong cluster addresses:
              functionsystem: %s
                  datasystem: %s

YuanRong cluster status:
  current running agents: %s
`, info.MasterIP+":"+info.BusPort, info.MasterIP+":"+info.DsWorkerPort, readyAgentCount))
		return nil
	}
}

func countAgent() (string, error) {
	cfg := clientv3.Config{
		Endpoints:   []string{yrOpts.etcdEndpoint},
		DialTimeout: time.Second,
	}
	if yrOpts.etcdTlsEnable {
		cert, err := tls.LoadX509KeyPair(yrOpts.etcdClientCertFile, yrOpts.etcdClientKeyFile)
		if err != nil {
			colorprint.PrintFail(yrOpts.cmdIO.Out, "LoadX509KeyPair error", err.Error(), ".\n")
		}

		caCert, err := os.ReadFile(yrOpts.etcdCaFile)
		if err != nil {
			colorprint.PrintFail(yrOpts.cmdIO.Out, "Read ca file error", err.Error(), ".\n")
		}

		caCertPool := x509.NewCertPool()
		caCertPool.AppendCertsFromPEM(caCert)

		cfg.TLS = &tls.Config{
			RootCAs:      caCertPool,
			Certificates: []tls.Certificate{cert},
		}
	}

	client, err := clientv3.New(cfg)
	if err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.Out, "Connect to etcd server failed.", err.Error(), ".\n")
		return "", err
	}
	defer client.Close()

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()

	resp, err := client.Get(ctx, "/yr/readyAgentCount")
	if err != nil {
		colorprint.PrintFail(yrOpts.cmdIO.Out, "Connect to etcd server failed.", err.Error(), ".\n")
		return "", err
	}
	if resp == nil || len(resp.Kvs) != 1 {
		colorprint.PrintFail(yrOpts.cmdIO.Out, "Can not find agent count key", "", ".\n")
		return "", errors.New("can not find agent count key")
	}
	readyAgentCount := resp.Kvs[0].Value
	return string(readyAgentCount), nil
}
