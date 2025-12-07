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

// Package obs is the obs client
package obs

import (
	"crypto/tls"
	"net/http"
	"strings"
	"sync"

	"meta_service/common/utils"

	"github.com/huaweicloud/huaweicloud-sdk-go-obs/obs"

	commonTLS "meta_service/common/tls"
)

const (
	// caFile default Certificate authority file path
	caFile      = "/home/sn/module/ca.crt"
	httpPrefix  = "http://"
	httpsPrefix = "https://"
	uint64Base  = 10
	dirMode     = 0o700
	fileMode    = 0o600
	readSize    = 32 * 1024
)

var tlsConfig = struct {
	sync.Once
	config *tls.Config
}{}

var (
	secretKeyRecord []byte
	accessKeyRecord []byte
)

// Option contains the options to connect the OBS
type Option struct {
	Secure        bool
	AccessKey     string
	SecretKey     string
	Endpoint      string
	CaFile        string
	TrustedCA     bool
	MaxRetryCount int
	Timeout       int
}

// NewObsClient instantiate obs client, adds automatic verification of signature.
func NewObsClient(o Option) (*obs.ObsClient, error) {
	var endpoint string
	var urlPrefix string
	var err error

	var transport *http.Transport = nil

	endpoint, urlPrefix = getEndpoint(o.Endpoint)
	if o.Secure {
		// load ca certificate
		if o.CaFile == "" {
			o.CaFile = caFile
		}
		tlsConfig := getTLSConfig(o.CaFile, o.TrustedCA)
		transport = &http.Transport{
			TLSClientConfig:    tlsConfig,
			DisableCompression: true,
		}
		urlPrefix = httpsPrefix
	} else {
		endpoint, err = utils.Domain2IP(endpoint)
		if err != nil {
			return nil, err
		}
	}

	obsClient, err := obs.New(o.AccessKey, o.SecretKey, urlPrefix+endpoint,
		obs.WithSslVerify(o.Secure), obs.WithHttpTransport(transport), obs.WithMaxRetryCount(o.MaxRetryCount),
		obs.WithConnectTimeout(o.Timeout), obs.WithSocketTimeout(o.Timeout), obs.WithPathStyle(true))
	if err != nil {
		return nil, err
	}
	secretKeyRecord = []byte(o.SecretKey)
	accessKeyRecord = []byte(o.AccessKey)
	return obsClient, nil
}

// getTLSConfig get tls config
func getTLSConfig(ca string, trustedCA bool) *tls.Config {
	tlsConfig.Do(func() {
		if trustedCA {
			// No ca is required.
			tlsConfig.config = commonTLS.NewTLSConfig()
			tlsConfig.config.InsecureSkipVerify = true
		} else {
			tlsConfig.config = commonTLS.NewTLSConfig(commonTLS.WithRootCAs(ca))
		}
	})
	return tlsConfig.config
}

func getEndpoint(endpoint string) (string, string) {
	var urlPrefix string
	urlPrefix = httpPrefix
	if strings.HasPrefix(endpoint, httpPrefix) {
		endpoint = strings.TrimPrefix(endpoint, httpPrefix)
		urlPrefix = httpPrefix
	}
	if strings.HasPrefix(endpoint, httpsPrefix) {
		endpoint = strings.TrimPrefix(endpoint, httpsPrefix)
		urlPrefix = httpsPrefix
	}
	return endpoint, urlPrefix
}

// ClearSecretKeyMemory clear secretKey memory
func ClearSecretKeyMemory() {
	utils.ClearStringMemory(string(secretKeyRecord))
	utils.ClearStringMemory(string(accessKeyRecord))
}
