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

package model

import (
	"time"
)

// LayerQueryInfo contains layer name and version. usually parsed from a layerURN.
type LayerQueryInfo struct {
	LayerName    string
	LayerVersion int
}

// CreateLayerRequest is the request body when creating a layer.
type CreateLayerRequest struct {
	ZipFileSize        int64
	CompatibleRuntimes string
	Description        string
	LicenseInfo        string
}

// GetLayerListRequest is the request body when getting a layer.
type GetLayerListRequest struct {
	LayerName         string `form:"layerName" valid:"maxstringlength(32)"`
	LayerVersion      int    `form:"layerVersion" valid:"range(1|1000000)"`
	CompatibleRuntime string `form:"compatibleRuntime" valid:"maxstringlength(64)"`
	OwnerFlag         int    `form:"ownerFlag" valid:"range(0|2)"`
	PageIndex         int    `form:"pageIndex" valid:"range(1|1000)"`
	PageSize          int    `form:"pageSize" valid:"range(1|100)"`
}

// GetLayerVersionListRequest is used when getting layer version list.
type GetLayerVersionListRequest struct {
	LayerName         string
	CompatibleRuntime string
	PageIndex         int
	PageSize          int
}

// UpdateLayerRequest is the request body when updating a layer.
type UpdateLayerRequest struct {
	Description    string    `json:"description" valid:"maxstringlength(1024)"`
	LicenseInfo    string    `json:"licenseInfo" valid:"maxstringlength(64)"`
	LastUpdateTime time.Time `json:"lastUpdateTime" valid:",required"`
	Version        int       `json:"version" valid:"range(1|10000)"`
}

// Layer contains layer information.
type Layer struct {
	Name               string    `json:"name"`
	Version            int       `json:"versionNumber"`
	LayerURN           string    `json:"layerUrn"`
	LayerVersionURN    string    `json:"layerVersionUrn"`
	LayerSize          int64     `json:"layerSize"`
	LayerSHA256        string    `json:"layerSha256"`
	CreateTime         time.Time `json:"createTime"`
	UpdateTime         time.Time `json:"updateTime"`
	CompatibleRuntimes []string  `json:"compatibleRuntimes"`
	Description        string    `json:"description"`
	LicenseInfo        string    `json:"licenseInfo"`
}

// LayerList is a list of layers.
type LayerList struct {
	Total         int     `json:"total"`
	LayerVersions []Layer `json:"layerVersions"`
}
