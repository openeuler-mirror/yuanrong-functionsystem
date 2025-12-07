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

// Package constants implements definitions of constants for common use
package constants

// repo constants value
const (
	// DefaultPageSize default data size to be queried
	DefaultPageSize = "1000"
	// DefaultPageIndex default query page number
	DefaultPageIndex = "1"
	// ETCDKeySeparator is etcd key Separator
	ETCDKeySeparator = "/"
	// RandomKeySize length of the random key. The agreed length is 32,not advised to change the value.
	RandomKeySize = 32
	// WorkKeyConfig key for encrypting the configuration file
	WorkKeyConfig = "config-key"
	// EtcdKeyConfig key for encrypting the ETCD file
	EtcdKeyConfig = "etcd-key"
	// NilStringValue is an empty string.
	NilStringValue = ""
	// DefaultVersion is default function name
	DefaultVersion = "$latest"
	// MetadataPrefix the prefix of function metadata in etcd
	MetadataPrefix = "/repo"
	// InstancePrefix the prefix of instance metadata in etcd
	InstancePrefix = "/instances"
	// FunctionPrefix the prefix of published functions metadata in etcd
	FunctionPrefix = "/sn/functions"
	// YRFunctionPrefix is the prefix of published yr functions metadata in etcd
	YRFunctionPrefix = "/yr/functions"
	// TriggerPrefix the prefix of published triggers metadata in etcd
	TriggerPrefix = "/sn/triggers"
	// AliasPrefix the prefix of published aliases metadata in etcd
	AliasPrefix = "/sn/aliases"
	// ChainPrefix the prefix of published chains metadata in etcd
	ChainPrefix = "/sn/functionchains"
	// PodPoolPrefix the prefix of published pod pools metadata in etcd
	PodPoolPrefix = "/yr/podpools/info"
	// BusinessKey the key of business id
	BusinessKey = "business"
	// TenantKey the key of tenant id
	TenantKey = "tenant"
	// ResourceKey the key of function name
	ResourceKey = "function"
	// VersionKey the key of version
	VersionKey = "version"
	// TriggerTypeKey the key of trigger type
	TriggerTypeKey = "triggerType"
	// ClusterKey the key of cluster id
	ClusterKey = "cluster"
	// LabelKey -
	LabelKey = "label"
	// DefaultClusterID -
	DefaultClusterID = "cluster001"
	// Faas kind of function creation
	Faas = "faas"
)

const (
	// New pod pool status: New
	New int = iota
	// Creating pod pool status: Creating
	Creating
	// Update pod pool status: Update
	Update
	// Updating pod pool status: Updating
	Updating
	// Running pod pool status: Running
	Running
	// Failed pod pool status: Failed
	Failed
	// Deleted pod pool status: Deleted
	Deleted
)
