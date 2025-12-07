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

package storage

import (
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"

	"meta_service/common/logger/log"
)

func genFunctionResourceIDIndexKey(t server.TenantInfo, funcName, verOrAlias, resourceID string,
) FunctionResourceIDIndexKey {
	return FunctionResourceIDIndexKey{
		TenantInfo:     t,
		FunctionName:   funcName,
		VersionOrAlias: verOrAlias,
		ResourceID:     resourceID,
	}
}

// HTTPTriggerEtcdSpec defines HTTP trigger spec.
type HTTPTriggerEtcdSpec struct {
	HTTPMethod    string
	ResourceID    string
	AuthFlag      bool
	AuthAlgorithm string
	TriggerURL    string
	AppID         string
	AppSecret     string
}

// BuildModelSpec implements triggerSpec
func (spec HTTPTriggerEtcdSpec) BuildModelSpec(tspec model.TriggerSpec) (interface{}, error) {
	var httpSpec model.HTTPTriggerSpec
	httpSpec.TriggerSpec = tspec
	httpSpec.HTTPMethod = spec.HTTPMethod
	httpSpec.ResourceID = spec.ResourceID
	httpSpec.AuthFlag = spec.AuthFlag
	httpSpec.AuthAlgorithm = spec.AuthAlgorithm
	httpSpec.TriggerURL = spec.TriggerURL
	httpSpec.AppID = spec.AppID
	httpSpec.AppSecret = spec.AppSecret
	return &httpSpec, nil
}

func (spec *HTTPTriggerEtcdSpec) save(txn Transaction, funcName, verOrAlias string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	frKey := genFunctionResourceIDIndexKey(t, funcName, verOrAlias, spec.ResourceID)
	_, err = txn.GetTxn().FunctionResourceIDIndexGet(frKey)
	if err == nil {
		return errmsg.NewParamError("the functionID and resourceID exists")
	}
	if err != errmsg.KeyNotFoundError {
		log.GetLogger().Errorf("failed to get function resource ID index, error: %s", err.Error())
		return err
	}
	if err := txn.GetTxn().FunctionResourceIDIndexPut(frKey, FunctionResourceIDIndexValue{}); err != nil {
		log.GetLogger().Errorf("failed to save function resource ID index, error: %s", err.Error())
		return err
	}
	return nil
}

func (spec *HTTPTriggerEtcdSpec) delete(txn Transaction, funcName, verOrAlias string) error {
	resourceID := spec.ResourceID
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if verOrAlias != "" {
		frKey := genFunctionResourceIDIndexKey(t, funcName, verOrAlias, resourceID)
		if err := txn.GetTxn().FunctionResourceIDIndexDelete(frKey); err != nil {
			log.GetLogger().Errorf("failed to delete function resource ID index, error: %s", err.Error())
			return err
		}
	} else {
		frKey := genFunctionResourceIDIndexKey(t, funcName, "", "")
		if err := txn.GetTxn().FunctionResourceIDIndexDeleteRange(frKey); err != nil {
			log.GetLogger().Errorf("failed to delete function resource ID index, error: %s", err.Error())
			return err
		}
	}
	return nil
}
