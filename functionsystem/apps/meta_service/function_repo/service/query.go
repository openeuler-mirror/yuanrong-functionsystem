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

package service

import (
	"fmt"
	"regexp"
	"strings"

	"meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/common/urnutils"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils"
)

const (
	// the alias name can contain a maximum of 16 characters and a minimum of 2 characters,
	// including lowercase letters, digits, and hyphens (-).
	// it must start with a lowercase letter and end with a lowercase letter and digit
	aliasNameRegex = "^[a-z][0-9a-z-]{0,14}[0-9a-z]$"
	// version can contain a maximum of 16 characters and a minimum of 1 characters,
	// including "$latest", "latest" or only digits.
	// UPDATE: now version can contain digits(0-9), letters(a-z, A-Z), dots(.), hyphens(-), and underscores(_),
	// and must start and end with a digit(0-9) or letter(a-z, A-Z), with less than 42 characters
	versionRegex = "^[\\$]?latest$|^[0-9]{1,16}$|^[A-Za-z0-9][A-Za-z0-9._-]{0,40}[A-Za-z0-9]$|^[A-Za-z0-9]$"
	// The function name can contain the function name with or without the service ID.
	// Function name without serviceid must start with a lowercase letter and end with a lowercase letter or digit.
	// Only lowercase letters, digits, and hyphens (-) or (@) are supported. The function name contains 1 to 128 characters
	// Function name with serviceid add a serviceid based on the function name without serviceid.
	// serviceid must start with 0- and end with a hyphen (-) or (@), and contain 1 to 16 digits or letters.
	funcNameRegex = "(^0-[0-9a-zA-Z]{1,16}-([a-z0-9][a-z0-9-]{0,126}[a-z0-9]|[a-z])$)|" +
		"(^[a-z][a-z0-9-]{0,126}[a-z0-9]$|^[a-z]$)|(^0@[0-9a-zA-Z]{1,16}@([a-z0-9][a-z0-9-]{0,126}[a-z0-9]|[a-z])$)"
)

func parseTenantInfo(tenantInfo string) (string, string) {
	var productID, tenantID string
	index := strings.Index(tenantInfo, urnutils.TenantProductSplitStr)
	if index > 0 {
		// tenantInfo format is tenantID@productID
		productID = tenantInfo[index+1:]
		tenantID = tenantInfo[0:index]
		return productID, tenantID
	}
	return "", tenantInfo
}

func checkContextInfo(ctx server.Context, funcID string) error {
	tenantInfo, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenantinfo, error: %s", err.Error())
		return err
	}

	info, ok := utils.ParseTriggerInfoFromURN(funcID)
	if !ok {
		log.GetLogger().Errorf("invalid URN: %s", funcID)
		return errmsg.NewParamError(fmt.Sprintf("invalid URN: %s", funcID))
	}

	productID, tenantID := parseTenantInfo(info.FunctionInfo.TenantInfo)

	if tenantInfo.BusinessID != info.FunctionInfo.BusinessID ||
		tenantInfo.ProductID != productID || tenantInfo.TenantID != tenantID {
		log.GetLogger().Errorf("urnTenantInfo is not the same as tenantInfo from request header")
		return snerror.NewWithFmtMsg(errmsg.InvalidQueryURN, "invalid URN: %s", funcID)
	}
	return nil
}

func checkFunctionName(name string) error {
	match, err := regexp.MatchString(funcNameRegex, name)
	if err != nil {
		return err
	}
	if !match {
		return snerror.NewWithFmtMsg(errmsg.FunctionNameFormatErr, "invalid function name: %s", name)
	}
	return nil
}

func checkVersion(version, qualifier string) (string, error) {
	match, err := regexp.MatchString(versionRegex, version)
	if err != nil {
		log.GetLogger().Errorf("incorrect version format, error: %s", err.Error())
		return "", err
	}
	if match {
		return version, nil
	}
	if qualifier == "" {
		err = snerror.NewWithFmtMsg(errmsg.InvalidQueryURN, "invalid version: %s", version)
		log.GetLogger().Errorf("invalid version: %s", version)
	} else {
		err = snerror.NewWithFmtMsg(errmsg.InvalidQualifier, "invalid qualifier: %s", qualifier)
		log.GetLogger().Errorf("invalid qualifier: %s", qualifier)
	}
	return "", err
}

func checkVerOrAlias(verOrAlias, qualifier string) (string, string, error) {
	if verOrAlias == "" && qualifier == "" {
		return "", "", nil
	}
	if verOrAlias == "" && qualifier != "" {
		verOrAlias = qualifier
	}
	// check aliasName
	match, err := regexp.MatchString(aliasNameRegex, verOrAlias)
	if err == nil && match && verOrAlias != constants.DefaultLatestFaaSVersion {
		return "", verOrAlias, nil
	}

	// check version
	version, err := checkVersion(verOrAlias, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to check version, error: %s", err.Error())
		return "", "", err
	}
	return version, "", nil
}

// CheckAndGetVerOrAlias check function version and alias name
func CheckAndGetVerOrAlias(funcID, qualifier string) (model.FunctionQueryInfo, error) {
	var tmpAlias string
	funcInfo := model.FunctionQueryInfo{}

	info, ok := utils.ParseTriggerInfoFromURN(funcID)
	if !ok {
		log.GetLogger().Errorf("failed to Parse urn, URN: %s", funcID)
		return model.FunctionQueryInfo{}, errmsg.NewParamError(fmt.Sprintf("invalid URN: %s", funcID))
	}

	err := checkFunctionName(info.FunctionInfo.FunctionName)
	if err != nil {
		return model.FunctionQueryInfo{}, err
	}
	funcInfo.FunctionName = info.FunctionInfo.FunctionName
	tmpAlias = info.VerOrAlias

	if tmpAlias != "" && qualifier != "" && qualifier != tmpAlias {
		log.GetLogger().Errorf(
			"queryInfo is not the same as qualifier, urn is %s, qualifier is %s", funcID, qualifier)
		return model.FunctionQueryInfo{}, snerror.NewWithFmtMsg(errmsg.InvalidQueryURN, "invalid URN: %s", funcID)
	}

	funcInfo.FunctionVersion, funcInfo.AliasName, err = checkVerOrAlias(tmpAlias, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to get version or alias, error: %s", err.Error())
		return model.FunctionQueryInfo{}, err
	}
	return funcInfo, nil
}

func parseFunctionInfoByFuncID(
	ctx server.Context, funcID string, qualifier string,
) (model.FunctionQueryInfo, error) {
	if err := checkContextInfo(ctx, funcID); err != nil {
		log.GetLogger().Errorf("failed to check functionID, error: %s", err.Error())
		return model.FunctionQueryInfo{}, err
	}
	funcInfo, err := CheckAndGetVerOrAlias(funcID, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to check version or aliasname, error: %s", err.Error())
		return model.FunctionQueryInfo{}, err
	}
	return funcInfo, nil
}

func parseFunctionInfoByName(funcName string, qualifier string) (model.FunctionQueryInfo, error) {
	info := model.FunctionQueryInfo{}
	info.FunctionName = funcName

	if qualifier == "" {
		return info, nil
	}

	match, err := regexp.MatchString(aliasNameRegex, qualifier)
	if err == nil && match && qualifier != utils.GetDefaultFaaSVersion() {
		info.AliasName = qualifier
		return info, nil
	}

	if match, err = regexp.MatchString(versionRegex, qualifier); err == nil && match {
		info.FunctionVersion = qualifier
		return info, nil
	}
	err = snerror.NewWithFmtMsg(errmsg.InvalidQualifier, "invalid qualifier: %s", qualifier)
	log.GetLogger().Errorf("invalid qualifier: %s", qualifier)
	return model.FunctionQueryInfo{}, err
}

// ParseFunctionInfo gets function info
func ParseFunctionInfo(ctx server.Context, queryInfo, qualifier string) (model.FunctionQueryInfo, error) {
	var info model.FunctionQueryInfo
	if queryInfo == "" {
		log.GetLogger().Errorf("empty functionid")
		return model.FunctionQueryInfo{}, snerror.NewWithFmtMsg(errmsg.InvalidQueryURN, "empty functionid")
	}

	err := checkFunctionName(queryInfo)
	if err != nil {
		info, err = parseFunctionInfoByFuncID(ctx, queryInfo, qualifier)
		if err != nil {
			log.GetLogger().Errorf("failed to get function info by functionID, err: %s", err.Error())
			return model.FunctionQueryInfo{}, err
		}
		return info, nil
	}
	info, err = parseFunctionInfoByName(queryInfo, qualifier)
	if err != nil {
		log.GetLogger().Errorf("failed to get function info by name, err: %s", err.Error())
		return model.FunctionQueryInfo{}, err
	}
	return info, nil
}
