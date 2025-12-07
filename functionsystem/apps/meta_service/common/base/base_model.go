/*
 * Copyright (c) 2025 Huawei Technologies Co., Ltd
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

// Package base provides base model.
package base

// FunctionCommonInfo function common info
type FunctionCommonInfo struct {
	ID                  string             `json:"id"`
	CreateTime          string             `json:"createTime"`
	UpdateTime          string             `json:"updateTime"`
	FunctionURN         string             `json:"functionUrn"`
	FunctionName        string             `json:"name"`
	TenantID            string             `json:"tenantId"`
	BusinessID          string             `json:"businessId"`
	ProductID           string             `json:"productId"`
	ReversedConcurrency int                `json:"reversedConcurrency"`
	Description         string             `json:"description"`
	LastModified        string             `json:"lastModified"`
	Published           string             `json:"Published"`
	MinInstance         int                `json:"minInstance"`
	MaxInstance         int                `json:"maxInstance"`
	ConcurrentNum       int                `json:"concurrentNum"`
	Status              string             `json:"status"`
	InstanceNum         int                `json:"instanceNum"`
	Tag                 map[string]string  `json:"tag"`
	FunctionVersionURN  string             `json:"functionVersionUrn"`
	RevisionID          string             `json:"revisionId"`
	CodeSize            int64              `json:"codeSize"`
	CodeSha256          string             `json:"codeSha256"`
	BucketID            string             `json:"bucketId"`
	ObjectID            string             `json:"objectId"`
	Handler             string             `json:"handler"`
	Layers              []string           `json:"layers"`
	CPU                 int                `json:"cpu"`
	Memory              int                `json:"memory"`
	Runtime             string             `json:"runtime"`
	Timeout             int                `json:"timeout"`
	VersionNumber       string             `json:"versionNumber"`
	VersionDesc         string             `json:"versionDesc"`
	Environment         map[string]string  `json:"environment"`
	CustomResources     map[string]float64 `json:"customResources"`
	StatefulFlag        int                `json:"statefulFlag"`
}
