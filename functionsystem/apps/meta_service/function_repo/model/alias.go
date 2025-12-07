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

import "errors"

const (
	// limit routingConfig to 16 in code, cli limit routingConfig to 2
	maxRoutingConfigNum = 16
)

// AliasRequest is alias create request
type AliasRequest struct {
	Name            string         `json:"name" valid:"matches(^[a-z][0-9a-z-]{0\,14}[0-9a-z]$),maxstringlength(16)"`
	FunctionVersion string         `json:"functionVersion" valid:"maxstringlength(16)"`
	Description     string         `json:"description,omitempty" valid:"maxstringlength(1024)"`
	RoutingConfig   map[string]int `json:"routingConfig" valid:"-"`
}

// Validate validate alias create request
func (r *AliasRequest) Validate() error {
	if len(r.RoutingConfig) > maxRoutingConfigNum {
		return errors.New("alias router number is out of range")
	}
	return nil
}

// AliasUpdateRequest is alias update request
type AliasUpdateRequest struct {
	FunctionVersion string         `json:"functionVersion" valid:"maxstringlength(16)"`
	Description     string         `json:"description,omitempty" valid:"maxstringlength(1024)"`
	RevisionID      string         `json:"revisionId" valid:"maxstringlength(20)"`
	RoutingConfig   map[string]int `json:"routingConfig,omitempty" valid:"-"`
}

// Validate validate alias update request
func (r *AliasUpdateRequest) Validate() error {
	if len(r.RoutingConfig) > maxRoutingConfigNum {
		return errors.New("alias router number is out of range")
	}
	return nil
}

// AliasQueryRequest is alias query request
type AliasQueryRequest struct {
	FunctionName string `json:"functionName"`
	AliasName    string
}

// AliasListQueryRequest is alias list query request
type AliasListQueryRequest struct {
	FunctionName    string
	FunctionVersion string
	PageIndex       int
	PageSize        int
}

// AliasDeleteRequest is alias delete request
type AliasDeleteRequest struct {
	FunctionName string
	AliasName    string
}

// AliasResponse is base response of alias operation
type AliasResponse struct {
	AliasURN        string         `json:"aliasUrn"`
	Name            string         `json:"name"`
	FunctionVersion string         `json:"functionVersion"`
	Description     string         `json:"description,omitempty"`
	RevisionID      string         `json:"revisionId"`
	RoutingConfig   map[string]int `json:"routingConfig"`
}

// AliasCreateResponse is response of alias create
type AliasCreateResponse struct {
	AliasResponse
}

// AliasUpdateResponse is response of alias update
type AliasUpdateResponse struct {
	AliasResponse
}

// AliasQueryResponse is response of alias query
type AliasQueryResponse struct {
	AliasResponse
}

// AliasListQueryResponse is response of alias list query
type AliasListQueryResponse struct {
	Total   int             `json:"total"`
	Aliases []AliasResponse `json:"aliases"`
}
