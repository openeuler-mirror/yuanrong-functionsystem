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

// Package config implements config map of repository
package config

import (
	"encoding/json"
	"os"
	"strings"

	"github.com/asaskevich/govalidator/v11"

	"meta_service/common/etcd3"
	"meta_service/common/logger/log"
	"meta_service/common/reader"
	"meta_service/common/tls"
	"meta_service/common/utils"
	"meta_service/function_repo/utils/constants"
)

const (
	// DefaultTimeout define a defaultTimeout
	DefaultTimeout = 900

	envS3AccessKey = "S3_ACCESS_KEY"
	envS3SecretKey = "S3_SECRET_KEY"
)

// RepoCfg is config map of repository
var RepoCfg *Configs

// FunctionType is a type of function, only use service now
type FunctionType string

const (
	// ComFunctionType common function type
	ComFunctionType FunctionType = "service"

	caFilePath = "/home/sn/resource/etcd/ca.crt"

	certFilePath = "/home/sn/resource/etcd/client.crt"

	keyFilePath = "/home/sn/resource/etcd/client.key"

	passphraseFilePath = "/home/sn/resource/etcd/passphrase"
)

type envConfig struct {
	TimeZone     string `json:"timeZone" valid:"stringlength(1|255)"`
	NodejsLdPath string `json:"nodejsLdPath" valid:"stringlength(1|255)"`
	NodejsPath   string `json:"nodejsPath" valid:"stringlength(1|255)"`
	JavaLdPath   string `json:"javaLdPath" valid:"stringlength(1|255)"`
	JavaPath     string `json:"javaPath" valid:"stringlength(1|255)"`
	CppLdPath    string `json:"cppLdPath" valid:"stringlength(1|255)"`
	PythonLdPath string `json:"pythonLdPath" valid:"stringlength(1|255)"`
	PythonPath   string `json:"pythonPath" valid:"stringlength(1|255)"`
}

type funcDefault struct {
	Version                 string  `json:"version" valid:"stringlength(1|32)"`
	EnvPrefix               string  `json:"envPrefix" valid:"stringlength(1|32)"`
	PageIndex               uint    `json:"pageIndex" valid:"numeric"`
	PageSize                uint    `json:"pageSize" valid:"numeric"`
	CPUList                 []int64 `json:"cpuList"`
	MemoryList              []int64 `json:"memoryList"`
	Timeout                 int64   `json:"timeout" valid:"numeric"`
	DefaultMinInstance      string  `json:"defaultMinInstance"`
	DefaultMaxInstance      string  `json:"defaultMaxInstance"`
	DefaultConcurrentNum    string  `json:"defaultConcurrentNum"`
	MaxInstanceUpperLimit   string  `json:"maxInstanceUpperLimit"`
	ConcurrentNumUpperLimit string  `json:"concurrentNumUpperLimit"`
	MinCPU                  int64   `json:"minCpu" valid:"numeric"`
	MinMemory               int64   `json:"minMemory" valid:"numeric"`
	MaxCPU                  int64   `json:"maxCpu" valid:"numeric"`
	MaxMemory               int64   `json:"maxMemory" valid:"numeric"`
}

// PackageConfig defines the configuration for function packages' and layer packages' validations.
type PackageConfig struct {
	UploadTmpPath      string `json:"uploadTempPath,omitempty" valid:"optional"`
	ZipFileSizeMaxMB   uint   `json:"zipFileSizeMaxMB" valid:"numeric"`
	UnzipFileSizeMaxMB uint   `json:"unzipFileSizeMaxMB" valid:"numeric"`
	FileCountsMax      uint   `json:"fileCountsMax" valid:"numeric"`
	DirDepthMax        uint   `json:"dirDepthMax" valid:"numeric"`
	IOReadTimeout      uint   `json:"ioReadTimeout" valid:"numeric,required"`
}

// BucketConfig defines bucket configuration for tenant, each tenant may have one or more buckets.
type BucketConfig struct {
	BucketID   string `json:"bucketId" valid:"stringlength(1|255)"`
	BusinessID string `json:"businessId" valid:"stringlength(1|255)"`
	AppID      string `json:"appId" valid:"stringlength(1|255)"`
	AppSecret  string `json:"appSecret" valid:"stringlength(1|255)"`
	URL        string `json:"url" valid:"stringlength(1|255)"`
	Writable   int    `json:"writable" valid:"in(0|1)"`
	Desc       string `json:"description" valid:"stringlength(1|255)"`
	CreateTime string `json:"createTime,omitempty" valid:"optional"`
	UpdateTime string `json:"updateTime,omitempty" valid:"optional"`
}

type funcConfig struct {
	DefaultCfg       funcDefault   `json:"default,omitempty" valid:"optional"`
	PackageCfg       PackageConfig `json:"package,omitempty" valid:"optional"`
	VersionMax       uint          `json:"versionMax" valid:"optional"`
	AliasMax         uint          `json:"aliasMax" valid:"optional"`
	LayerMax         int           `json:"layerMax" valid:"optional"`
	InstanceLabelMax int           `json:"instanceLabelMax" valid:"optional"`
}

type triggerType struct {
	SourceProvider string `json:"sourceProvider" valid:"stringlength(1|255)"`
	Effect         string `json:"effect" valid:"stringlength(1|255)"`
	Action         string `json:"action" valid:"stringlength(1|255)"`
}

type triggerConfig struct {
	URLPrefix   string        `json:"urlPrefix" valid:"url"`
	TriggerType []triggerType `json:"type" valid:"required"`
}

type s3Config struct {
	Endpoint   string `json:"endpoint" valid:"url,required"`
	AccessKey  string `json:"accessKey" valid:"stringlength(1|255)"`
	SecretKey  string `json:"secretKey" valid:"stringlength(1|255)"`
	Secure     bool   `json:"secure"`
	URLExpires uint   `json:"presignedUrlExpires" valid:"numeric"`
	Timeout    uint   `json:"timeout" valid:"numeric"`
	CaFile     string `json:"caFile,omitempty" valid:"optional"`
	TrustedCA  bool   `json:"trustedCA,omitempty" valid:"optional"`
}

type fileServerConfig struct {
	StorageType string   `json:"storageType" valid:"required"`
	S3          s3Config `json:"s3" valid:"optional"`
}

type urnConfig struct {
	Prefix       string `json:"prefix" valid:"stringlength(1|16)"`
	Zone         string `json:"zone" valid:"stringlength(1|16)"`
	ResourceType string `json:"resourceType" valid:"stringlength(1|16)"`
}

type serverCfg struct {
	IP   string `json:"ip"`
	Port uint   `json:"port"`
}

// Configs dump config from file
type Configs struct {
	EtcdCfg               etcd3.EtcdConfig    `json:"etcd" valid:"required"`
	MetaEtcdEnable        bool                `json:"metaEtcdEnable"`
	MetaEtcdCfg           etcd3.EtcdConfig    `json:"metaEtcd"`
	FunctionCfg           funcConfig          `json:"function" valid:"required"`
	TriggerCfg            triggerConfig       `json:"trigger" valid:"required"`
	BucketCfg             []BucketConfig      `json:"bucket" valid:"required"`
	RuntimeType           []string            `json:"runtimeType" valid:"required"`
	CompatibleRuntimeType []string            `json:"compatibleRuntimeType" valid:"required"`
	FileServer            fileServerConfig    `json:"fileServer" valid:"required"`
	URNCfg                urnConfig           `json:"urn"`
	EnvCfg                envConfig           `json:"env"`
	ServerCfg             serverCfg           `json:"server"`
	MutualTLSConfig       tls.MutualTLSConfig `json:"mutualTLSConfig" valid:"optional"`
	MutualSSLConfig       tls.MutualSSLConfig `json:"mutualSSLConfig" valid:"optional"`
	DecryptAlgorithm      string              `json:"decryptAlgorithm" valid:"optional"`
	Clusters              string              `json:"clusters"`
	ClusterID             map[string]bool     `json:"-"`
}

// InitConfig get config info from configPath
func InitConfig(filename string) error {
	data, err := reader.ReadFileWithTimeout(filename)
	if err != nil {
		log.GetLogger().Errorf("failed to read config, filename: %s, error: %s", filename, err.Error())
		return err
	}

	RepoCfg = new(Configs)

	err = json.Unmarshal(data, RepoCfg)
	if err != nil {
		log.GetLogger().Errorf("failed to unmarshal config, configPath: %s, error: %s", filename, err.Error())
		return err
	}
	if RepoCfg.EtcdCfg.AuthType == "TLS" {
		setEtcdResourceCerts()
	} else {
		RepoCfg.EtcdCfg = etcd3.GetETCDCertificatePath(RepoCfg.EtcdCfg, RepoCfg.MutualTLSConfig)
		RepoCfg.MetaEtcdCfg = etcd3.GetETCDCertificatePath(RepoCfg.MetaEtcdCfg, RepoCfg.MutualTLSConfig)
	}

	utils.ValidateTimeout(&RepoCfg.FunctionCfg.DefaultCfg.Timeout, DefaultTimeout)
	_, err = govalidator.ValidateStruct(RepoCfg)
	if err != nil {
		log.GetLogger().Errorf("failed to validate config, err: %s", err.Error())
		return err
	}
	SetTLSConfig(RepoCfg)
	setS3AccessInfoByEnv(RepoCfg)
	RepoCfg.ClusterID = make(map[string]bool)
	RepoCfg.ClusterID[constants.DefaultClusterID] = true
	if RepoCfg.Clusters != "" {
		splits := strings.Split(RepoCfg.Clusters, ",")
		for _, item := range splits {
			if item != "" {
				RepoCfg.ClusterID[item] = true
			}
		}
	}
	return nil
}

func setS3AccessInfoByEnv(cfg *Configs) {
	// the ak sk will be covered by ENV
	// so whether keep the env same with values in config file
	// or make it empty, so it will use the values in config file
	if ak := os.Getenv(envS3AccessKey); ak != "" {
		cfg.FileServer.S3.AccessKey = ak
	}
	if sk := os.Getenv(envS3SecretKey); sk != "" {
		cfg.FileServer.S3.SecretKey = sk
	}
}

func setEtcdResourceCerts() {
	if RepoCfg.EtcdCfg.CaFile == "" {
		RepoCfg.EtcdCfg.CaFile = caFilePath
	}
	if RepoCfg.EtcdCfg.CertFile == "" {
		RepoCfg.EtcdCfg.CertFile = certFilePath
	}
	if RepoCfg.EtcdCfg.KeyFile == "" {
		RepoCfg.EtcdCfg.KeyFile = keyFilePath
	}
	if RepoCfg.EtcdCfg.PassphraseFile == "" {
		RepoCfg.EtcdCfg.PassphraseFile = passphraseFilePath
	}
	if RepoCfg.MetaEtcdCfg.CaFile == "" {
		RepoCfg.MetaEtcdCfg.CaFile = caFilePath
	}
	if RepoCfg.MetaEtcdCfg.CertFile == "" {
		RepoCfg.MetaEtcdCfg.CertFile = certFilePath
	}
	if RepoCfg.MetaEtcdCfg.KeyFile == "" {
		RepoCfg.MetaEtcdCfg.KeyFile = keyFilePath
	}
	if RepoCfg.MetaEtcdCfg.PassphraseFile == "" {
		RepoCfg.MetaEtcdCfg.PassphraseFile = passphraseFilePath
	}
}

func SetTLSConfig(metaServCfg *Configs) {
	metaServCfg.MutualTLSConfig.TLSEnable = metaServCfg.MutualSSLConfig.SSLEnable
	metaServCfg.MutualTLSConfig.RootCAFile = metaServCfg.MutualSSLConfig.RootCAFile
	metaServCfg.MutualTLSConfig.ModuleCertFile = metaServCfg.MutualSSLConfig.ModuleCertFile
	metaServCfg.MutualTLSConfig.ModuleKeyFile = metaServCfg.MutualSSLConfig.ModuleKeyFile
	metaServCfg.MutualTLSConfig.PwdFile = metaServCfg.MutualSSLConfig.PwdFile
	metaServCfg.MutualTLSConfig.ServerName = metaServCfg.MutualSSLConfig.ServerName
	metaServCfg.MutualTLSConfig.DecryptTool = metaServCfg.MutualSSLConfig.DecryptTool
}
