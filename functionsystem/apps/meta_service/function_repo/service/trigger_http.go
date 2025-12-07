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
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"regexp"
	"sort"
	"strings"

	"meta_service/common/logger/log"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/utils"
)

const (
	defaultAlgorithm = "HMAC-SHA256"
	maxHashLen       = 12
	appidMaxLen      = 32
	resourceIDMaxLen = 256
	splitStr         = "/"

	// resourceIDRegex contains uppercase letters, lowercase letters, digits, hyphens (-) and Multi-level routing(/).
	resourceIDRegex = "^[a-z0-9A-Z-/.]+$"
)

// httpMethods must be sorted in ascending order
var httpMethods = [...]string{"DELETE", "GET", "POST", "PUT"}

func createHTTPTriggerSpec(
	txn *storage.Txn, raw interface{}, funcInfo model.FunctionQueryInfo, funcID, triggerID string,
) (storage.TriggerSpec, error) {
	httpSpec, ok := raw.(*model.HTTPTriggerSpec)
	if !ok {
		log.GetLogger().Errorf("failed to assert HTTPTriggerSpec")
		return nil, errors.New("assert HTTPTriggerSpec")
	}
	exist, err := storage.CheckResourceIDExist(
		txn, funcInfo.FunctionName, getVerOrAlias(funcInfo), httpSpec.ResourceID)
	if err != nil {
		log.GetLogger().Errorf("failed to get resourceID, error: %s", err.Error())
		return nil, err
	}
	if exist {
		log.GetLogger().Errorf("the functionID and resourceID exists")
		return nil, errmsg.New(errmsg.TriggerPathRepeated)
	}

	httpEtcdSpec := storage.HTTPTriggerEtcdSpec{}
	if err := parseHTTPTriggerSpec(httpSpec, funcID, triggerID); err != nil {
		log.GetLogger().Errorf("failed to parse httpspec, error: %s", err.Error())
		return nil, errmsg.NewParamError("invalid httpspec in request")
	}
	httpEtcdSpec.HTTPMethod = httpSpec.HTTPMethod
	httpEtcdSpec.ResourceID = httpSpec.ResourceID
	httpEtcdSpec.AuthFlag = httpSpec.AuthFlag
	httpEtcdSpec.AuthAlgorithm = httpSpec.AuthAlgorithm
	httpEtcdSpec.TriggerURL = httpSpec.TriggerURL
	httpEtcdSpec.AppID = httpSpec.AppID
	httpEtcdSpec.AppSecret = httpSpec.AppSecret
	return &httpEtcdSpec, nil
}

func parseHTTPTriggerSpec(spec *model.HTTPTriggerSpec, fid, tid string) error {
	urlPrefix := config.RepoCfg.TriggerCfg.URLPrefix
	spec.TriggerID = tid
	spec.FuncID = fid

	err := checkResourceID(spec.ResourceID)
	if err != nil {
		log.GetLogger().Errorf("failed to check resouceID, error: %s", err.Error())
		return err
	}
	if spec.AuthFlag {
		if err := checkHTTPTriggerAppInfo(spec); err != nil {
			log.GetLogger().Errorf("failed to check app info, error: %s", err.Error())
			return err
		}
	}
	if err := checkHTTPMethod(spec.HTTPMethod); err != nil {
		log.GetLogger().Errorf("failed to check http method, error: %s", err.Error())
		return err
	}
	appID, err := buildAppID(fid)
	if err != nil {
		log.GetLogger().Errorf("failed to build appID, error: %s", err.Error())
		return err
	}
	spec.TriggerURL = urlPrefix + appID + "/" + spec.ResourceID
	return nil
}

func checkResourceID(rid string) error {
	if rid == "" || len(rid) > resourceIDMaxLen {
		log.GetLogger().Errorf("invalid resourceID length: %d", len(rid))
		return errmsg.NewParamError("invalid resourceID length: %d", len(rid))
	}
	if match, err := regexp.MatchString(resourceIDRegex, rid); err != nil || !match {
		log.GetLogger().Errorf("incorrect resourceID format: %s", rid)
		return errmsg.NewParamError("incorrect resourceID format: %s", rid)
	}
	return nil
}

func checkHTTPTriggerAppInfo(spec *model.HTTPTriggerSpec) error {
	if spec.AuthAlgorithm == "" || spec.AuthAlgorithm == defaultAlgorithm {
		spec.AuthAlgorithm = defaultAlgorithm
		if spec.AppID == "" || len(spec.AppID) > appidMaxLen {
			log.GetLogger().Errorf("invalid appID len: %d", len(spec.AppID))
			return errmsg.NewParamError("invalid appID len: %d", len(spec.AppID))
		}
		if err := encryptHTTPTriggerAppSecret(spec); err != nil {
			log.GetLogger().Errorf("failed to encrypt appSecret, error: %s", err.Error())
			return err
		}
	}
	return nil
}

func encryptHTTPTriggerAppSecret(spec *model.HTTPTriggerSpec) error {
	var err error
	if spec.AppSecret == "" || len(spec.AppSecret) > appSecretMaxLen {
		log.GetLogger().Errorf("invalid appSecret: %s", spec.AppSecret)
		return errmsg.NewParamError("invalid appSecret: %s", spec.AppSecret)
	}

	if err != nil {
		log.GetLogger().Errorf("failed to encrpt appSecret, error: %s", err.Error())
		return err
	}
	return nil
}

func checkHTTPMethod(method string) error {
	methods := strings.Split(method, splitStr)
	for _, v := range methods {
		if idx := sort.SearchStrings(httpMethods[:], v); idx == len(httpMethods) || httpMethods[idx] != v {
			log.GetLogger().Errorf("invalid httpMethod: %s", v)
			return errmsg.NewParamError("invalid httpMethod: %s", v)
		}
	}
	return nil
}

func buildAppID(fid string) (string, error) {
	info, ok := utils.ParseTriggerInfoFromURN(fid)
	if !ok {
		return "", errmsg.NewParamError("invalid URN: %s", fid)
	}

	sum := sha256.Sum256([]byte(fid))
	hashCode := hex.EncodeToString(sum[:])
	if len(hashCode) > maxHashLen {
		hashCode = hashCode[:maxHashLen]
	}
	appID := config.RepoCfg.URNCfg.Prefix + ":" + config.RepoCfg.URNCfg.Zone + ":" + info.FunctionInfo.BusinessID +
		":" + "function:" + hashCode + ":" + info.FunctionInfo.FunctionName + ":" + info.VerOrAlias
	return appID, nil
}
