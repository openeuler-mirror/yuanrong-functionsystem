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

// Package urnutils contains URN element definitions and tools
package urnutils

import (
	"errors"
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"meta_service/common/functioncapability"
	"meta_service/common/logger/log"
)

// An example of a function URN: <ProductID>:<RegionID>:<BusinessID>:<TenantID>:<FunctionSign>:<FunctionName>:<Version>
// Indices of elements in BaseURN
const (
	// ProductIDIndex is the index of the product ID in a URN
	ProductIDIndex = iota
	// RegionIDIndex is the index of the region ID in a URN
	RegionIDIndex
	// BusinessIDIndex is the index of the business ID in a URN
	BusinessIDIndex
	// TenantIDIndex is the index of the tenant ID in a URN
	TenantIDIndex
	// FunctionSignIndex is the index of the product ID in a URN
	FunctionSignIndex
	// FunctionNameIndex is the index of the product name in a URN
	FunctionNameIndex
	// VersionIndex is the index of the version in a URN
	VersionIndex
	// URNLenWithVersion is the normal URN length with a version
	URNLenWithVersion
)

// An example of a function functionkey: <TenantID>/<FunctionName>/<Version>
const (
	// TenantIDIndex is the index of the tenant ID in a functionkey
	TenantIDIndexKey = iota
)

const (
	urnLenWithoutVersion = URNLenWithVersion - 1
	// URNSep is a URN separator of functions
	URNSep = ":"
	// FunctionKeySep is a functionkey separator of functions
	FunctionKeySep = "/"
	// DefaultURNProductID is the default product ID of a URN
	DefaultURNProductID = "sn"
	// DefaultURNRegion is the default region of a URN
	DefaultURNRegion = "cn"
	// DefaultURNFuncSign is the default function sign of a URN
	DefaultURNFuncSign  = "function"
	defaultURNLayerSign = "layer"
	// DefaultURNVersion is the default version of a URN
	DefaultURNVersion = "$latest"
	anonymization     = "****"
	anonymizeLen      = 3

	// BranchAliasPrefix is used to remove "!" from aliasing rules at the begining of "!"
	BranchAliasPrefix = 1
	// BranchAliasRule is an aliased rule that begins with an "!"
	BranchAliasRule        = "!"
	functionNameStartIndex = 2
	// ServiceNameIndex is index of service name in urn
	ServiceNameIndex = 1
	funcNameMinLen   = 3
	// DefaultFunctionMaxLen is max length of function name
	DefaultFunctionMaxLen = 63
)

var functionGraphFuncNameRegexp = regexp.MustCompile("^[a-zA-Z]([a-zA-Z0-9_-]*[a-zA-Z0-9])?$")

// BaseURN contains elements of a product URN. It can expand to FunctionURN, LayerURN and WorkerURN
type BaseURN struct {
	ProductID  string
	RegionID   string
	BusinessID string
	TenantID   string
	TypeSign   string
	Name       string
	Version    string
}

// String serializes elements of function URN struct to string
func (p *BaseURN) String() string {
	urn := fmt.Sprintf("%s:%s:%s:%s:%s:%s", p.ProductID, p.RegionID,
		p.BusinessID, p.TenantID, p.TypeSign, p.Name)
	if p.Version != "" {
		return fmt.Sprintf("%s:%s", urn, p.Version)
	}
	return urn
}

// ParseFrom parses elements from a function URN
func (p *BaseURN) ParseFrom(urn string) error {
	elements := strings.Split(urn, URNSep)
	urnLen := len(elements)
	if urnLen < urnLenWithoutVersion || urnLen > URNLenWithVersion {
		return fmt.Errorf("failed to parse urn from: %s, invalid length: %d", urn, urnLen)
	}
	p.ProductID = elements[ProductIDIndex]
	p.RegionID = elements[RegionIDIndex]
	p.BusinessID = elements[BusinessIDIndex]
	p.TenantID = elements[TenantIDIndex]
	p.TypeSign = elements[FunctionSignIndex]
	p.Name = elements[FunctionNameIndex]
	if urnLen == URNLenWithVersion {
		p.Version = elements[VersionIndex]
	}
	return nil
}

// StringWithoutVersion return string without version
func (p *BaseURN) StringWithoutVersion() string {
	return fmt.Sprintf("%s:%s:%s:%s:%s:%s", p.ProductID, p.RegionID,
		p.BusinessID, p.TenantID, p.TypeSign, p.Name)
}

// GetFunctionInfo collects function information from a URN
func GetFunctionInfo(urn string) (BaseURN, error) {
	var parsedURN BaseURN
	if err := parsedURN.ParseFrom(urn); err != nil {
		log.GetLogger().Errorf("error while parsing an URN: %s", err.Error())
		return BaseURN{}, errors.New("parsing an URN error")
	}
	return parsedURN, nil
}

// GetFuncInfoWithVersion collects function information and distinguishes if the URN contains a version
func GetFuncInfoWithVersion(urn string) (BaseURN, error) {
	parsedURN, err := GetFunctionInfo(urn)
	if err != nil {
		return parsedURN, err
	}
	if parsedURN.Version == "" {
		log.GetLogger().Errorf("incorrect URN length: %s", Anonymize(urn))
		return parsedURN, errors.New("incorrect URN length, no version")
	}
	return parsedURN, nil
}

// ParseAliasURN is used to remove "!" from the beginning of the alias
func ParseAliasURN(aliasURN string) string {
	elements := strings.Split(aliasURN, URNSep)
	if len(elements) == URNLenWithVersion {
		if strings.HasPrefix(elements[VersionIndex], BranchAliasRule) {
			elements[VersionIndex] = elements[VersionIndex][BranchAliasPrefix:]
		}
		return strings.Join(elements, ":")
	}
	return aliasURN
}

// GetAlias returns an alias
func (p *BaseURN) GetAlias() string {
	if p.Version == DefaultURNVersion {
		return ""
	}
	if _, err := strconv.Atoi(p.Version); err == nil {
		return ""
	}
	return p.Version
}

// GetAliasForFuncBranch returns an alias for function branch
func (p *BaseURN) GetAliasForFuncBranch() string {
	if strings.HasPrefix(p.Version, BranchAliasRule) {
		// remove "!" from the beginning of the alias
		return p.Version[BranchAliasPrefix:]
	}
	return ""
}

// Valid check whether the self-verification function name complies with the specifications.
func (p *BaseURN) Valid(functionCapability int) error {
	serviceID, functionName, err := GetFunctionNameAndServiceName(p.Name)
	if err != nil {
		log.GetLogger().Errorf("failed to get serviceID and functionName")
		return err
	}
	if functionCapability == functioncapability.Fusion {
		if !(functionGraphFuncNameRegexp.MatchString(serviceID) ||
			functionGraphFuncNameRegexp.MatchString(functionName)) {
			errmsg := "failed to match reg%s"
			log.GetLogger().Errorf(errmsg, functionGraphFuncNameRegexp)
			return fmt.Errorf(errmsg, functionGraphFuncNameRegexp)
		}
		if len(serviceID) > DefaultFunctionMaxLen || len(functionName) > DefaultFunctionMaxLen {
			errmsg := "serviceID or functionName's len is out of range %d"
			log.GetLogger().Errorf(errmsg, DefaultFunctionMaxLen)
			return fmt.Errorf(errmsg, DefaultFunctionMaxLen)
		}
	}
	return nil
}

// GetFunctionNameAndServiceName returns serviceName and FunctionName
func GetFunctionNameAndServiceName(funcName string) (string, string, error) {
	if strings.HasPrefix(funcName, ServiceIDPrefix) {
		split := strings.Split(funcName, Separator)
		if len(split) < funcNameMinLen {
			log.GetLogger().Errorf("incorrect function name length: %s", len(split))
			return "", "", errors.New("parsing a function name error")
		}
		return split[ServiceNameIndex], strings.Join(split[functionNameStartIndex:], Separator), nil
	}
	log.GetLogger().Errorf("incorrect function name: %s", funcName)
	return "", "", errors.New("parsing a function name error")
}

// Anonymize anonymize input str to xxx****xxx
func Anonymize(str string) string {
	if len(str) < anonymizeLen+1+anonymizeLen {
		return anonymization
	}
	return str[:anonymizeLen] + anonymization + str[len(str)-anonymizeLen:]
}

// AnonymizeTenantURN Anonymize tenant info in urn
func AnonymizeTenantURN(urn string) string {
	elements := strings.Split(urn, URNSep)
	urnLen := len(elements)
	if urnLen < urnLenWithoutVersion || urnLen > URNLenWithVersion {
		return urn
	}
	elements[TenantIDIndex] = Anonymize(elements[TenantIDIndex])
	return strings.Join(elements, URNSep)
}

// AnonymizeTenantKey Anonymize tenant info in functionkey
func AnonymizeTenantKey(functionKey string) string {
	elements := strings.Split(functionKey, FunctionKeySep)
	keyLen := len(elements)
	if TenantIDIndexKey >= keyLen {
		return functionKey
	}
	elements[TenantIDIndexKey] = Anonymize(elements[TenantIDIndexKey])
	return strings.Join(elements, FunctionKeySep)
}

// AnonymizeTenantURNSlice Anonymize tenant info in urn slice
func AnonymizeTenantURNSlice(urns []string) []string {
	var anonymizeUrns []string
	for i := 0; i < len(urns); i++ {
		anonymizeUrn := AnonymizeTenantURN(urns[i])
		anonymizeUrns = append(anonymizeUrns, anonymizeUrn)
	}
	return anonymizeUrns
}
