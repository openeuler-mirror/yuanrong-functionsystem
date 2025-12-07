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

package urnutils

import (
	"strings"

	"meta_service/common/constants"
)

// Separator is the current system special
var (
	Separator = "-"
)

const (
	// ServiceIDPrefix is the prefix of the function with serviceID.
	ServiceIDPrefix = "0"

	// DefaultSeparator is a character that separates functions and services.
	DefaultSeparator = "-"

	// ServicePrefix is the prefix of the function with serviceID.
	ServicePrefix = "0-"

	// FaaSServiceIDPrefix is the prefix of the function with serviceID.
	FaaSServiceIDPrefix = "0"

	// DefaultFaaSSeparator is a character that separates functions and services.
	DefaultFaaSSeparator = "@"

	// FaaSServicePrefix is the prefix of the function with serviceID.
	FaaSServicePrefix = "0@"

	// TenantProductSplitStr separator between a tenant and a product
	TenantProductSplitStr = "@"

	minEleSize = 3
)

// ComplexFuncName contains service ID and raw function name
type ComplexFuncName struct {
	prefix    string
	ServiceID string
	FuncName  string
}

// NewComplexFuncName -
func NewComplexFuncName(svcID, funcName string) *ComplexFuncName {
	return &ComplexFuncName{
		prefix:    ServiceIDPrefix,
		ServiceID: svcID,
		FuncName:  funcName,
	}
}

// ParseFrom parse ComplexFuncName from string
func (c *ComplexFuncName) ParseFrom(name string) *ComplexFuncName {
	fields := strings.Split(name, Separator)
	if len(fields) < minEleSize || fields[0] != ServiceIDPrefix {
		c.prefix = ""
		c.ServiceID = ""
		c.FuncName = name
		return c
	}
	idx := 0
	c.prefix = fields[idx]
	idx++
	c.ServiceID = fields[idx]
	// $prefix$separator$ServiceID$separator$FuncName equals name
	c.FuncName = name[(len(c.prefix) + len(Separator) + len(c.ServiceID) + len(Separator)):]
	return c
}

// ParseFromFaaS parse ComplexFuncName from string
func (c *ComplexFuncName) ParseFromFaaS(name string) *ComplexFuncName {
	fields := strings.Split(name, DefaultFaaSSeparator)
	if len(fields) < minEleSize || fields[0] != ServiceIDPrefix {
		c.prefix = ""
		c.ServiceID = ""
		c.FuncName = name
		return c
	}
	idx := 0
	c.prefix = fields[idx]
	idx++
	c.ServiceID = fields[idx]
	// $prefix$separator$ServiceID$separator$FuncName equals name
	c.FuncName = name[(len(c.prefix) + len(DefaultFaaSSeparator) + len(c.ServiceID) + len(DefaultFaaSSeparator)):]
	return c
}

// String -
func (c *ComplexFuncName) String() string {
	return strings.Join([]string{c.prefix, c.ServiceID, c.FuncName}, Separator)
}

// FaaSString -
func (c *ComplexFuncName) FaaSString() string {
	return strings.Join([]string{c.prefix, c.ServiceID, c.FuncName}, DefaultFaaSSeparator)
}

// GetSvcIDWithPrefix get serviceID with prefix from function name
func (c *ComplexFuncName) GetSvcIDWithPrefix() string {
	return c.prefix + Separator + c.ServiceID
}

// SetSeparator -
func SetSeparator(separator string) {
	if separator != "" {
		Separator = separator
	}
}

// GetPureFaaSFunctionName get pure functionName from complexFuncName, eq: 0@service@functionName
func GetPureFaaSFunctionName(complexFuncName string) string {
	c := &ComplexFuncName{}
	c.ParseFromFaaS(complexFuncName)
	return c.FuncName
}

// GetPureFaaSService get pure service from complexFuncName, eq: 0@service@functionName
func GetPureFaaSService(complexFuncName string) string {
	c := &ComplexFuncName{}
	c.ParseFromFaaS(complexFuncName)
	return c.ServiceID
}

// ComplexFaaSFuncName complex funcName for faaS function
func ComplexFaaSFuncName(serviceName, functionName string) string {
	name := functionName
	if serviceName != "" {
		c := NewComplexFuncName(serviceName, functionName)
		name = c.FaaSString()
	}
	return name
}

// GetFunctionVersion -
func GetFunctionVersion(kind string) string {
	if kind == constants.Faas {
		return constants.DefaultLatestFaaSVersion
	}
	return constants.DefaultLatestVersion
}
