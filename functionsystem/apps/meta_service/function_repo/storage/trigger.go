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
	"meta_service/function_repo/server"

	"meta_service/common/engine"
	"meta_service/common/logger/log"
)

func genTriggerKey(t server.TenantInfo, funcName string, verOrAlias string, triggerID string) TriggerKey {
	return TriggerKey{
		TenantInfo:     t,
		FunctionName:   funcName,
		VersionOrAlias: verOrAlias,
		TriggerID:      triggerID,
	}
}

func genTriggerFunctionIndexKey(t server.TenantInfo, triggerID string) TriggerFunctionIndexKey {
	return TriggerFunctionIndexKey{
		TenantInfo: t,
		TriggerID:  triggerID,
	}
}

// DeleteTriggerByFunctionNameVersion deletes trigger metadata and related index entries by
// function name and version in a transaction.
func DeleteTriggerByFunctionNameVersion(txn Transaction, funcName, verOrAlias string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	triggerPrefix := genTriggerKey(t, funcName, verOrAlias, "")
	tuples, err := txn.GetTxn().TriggerGetRange(triggerPrefix)
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger list, error: %s", err.Error())
		return err
	}
	for _, tuple := range tuples {
		if err := txn.GetTxn().TriggerFunctionIndexDelete(genTriggerFunctionIndexKey(t, tuple.Key.TriggerID)); err != nil {
			log.GetLogger().Errorf("failed to delete trigger function index, error: %s", err.Error())
			return err
		}
		if err := tuple.Value.EtcdSpec.delete(txn, funcName, verOrAlias); err != nil {
			log.GetLogger().Errorf("failed to delete trigger related, error: %s", err.Error())
			return err
		}
	}
	return txn.GetTxn().TriggerDeleteRange(triggerPrefix)
}

// CheckResourceIDExist returns true if resource id is used by trigger of specified function name and version/alias.
func CheckResourceIDExist(txn *Txn, funcName string, verOrAlias string, resourceID string) (bool, error) {
	if verOrAlias == "" {
		return false, errmsg.VersionOrAliasError
	}
	if resourceID == "" {
		return false, errmsg.ResourceIDError
	}
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return false, err
	}
	_, err = txn.txn.FunctionResourceIDIndexGet(genFunctionResourceIDIndexKey(t, funcName, verOrAlias, resourceID))
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("failed to get function resource id index, error: %s", err.Error())
		return false, err
	}
	return true, nil
}

// SaveTriggerInfo saves trigger entry by specified function name, version/alias and trigger id in a transaction.
// It will also save related index entries: trigger function index entry and resource id index entry.
func SaveTriggerInfo(txn *Txn, funcName string, verOrAlias string, triggerID string, val TriggerValue) error {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	if err := txn.txn.TriggerPut(genTriggerKey(t, funcName, verOrAlias, triggerID), val); err != nil {
		log.GetLogger().Errorf("failed to save trigger info, error: %s", err.Error())
		return err
	}

	triggerFuncInfo := TriggerFunctionIndexValue{
		FunctionName:   funcName,
		VersionOrAlias: verOrAlias,
	}
	if err := txn.txn.TriggerFunctionIndexPut(genTriggerFunctionIndexKey(t, triggerID), triggerFuncInfo); err != nil {
		log.GetLogger().Errorf("failed to save trigger function index, error: %s", err.Error())
		return err
	}

	return val.EtcdSpec.save(txn, funcName, verOrAlias)
}

// GetFunctionInfo gets function name and version/alias by specified trigger id in a transaction.
func GetFunctionInfo(txn *Txn, triggerID string) (TriggerFunctionIndexValue, error) {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return TriggerFunctionIndexValue{}, err
	}
	res, err := txn.txn.TriggerFunctionIndexGet(genTriggerFunctionIndexKey(t, triggerID))
	if err != nil {
		log.GetLogger().Debugf("failed to get trigger function index, error: %s", err.Error())
		return TriggerFunctionIndexValue{}, err
	}
	return res, nil
}

// GetTriggerInfo gets trigger info by specified function name, version/alias and trigger id in a transaction.
func GetTriggerInfo(txn *Txn, funcName string, verOrAlias string, triggerID string) (TriggerValue, error) {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return TriggerValue{}, err
	}

	tv, err := txn.txn.TriggerGet(genTriggerKey(t, funcName, verOrAlias, triggerID))
	if err != nil {
		log.GetLogger().Debugf("failed to get trigger info, error: %s", err.Error())
		return TriggerValue{}, err
	}

	return tv, nil
}

// DeleteTrigger deletes trigger entry by specified function name, version/alias and trigger id in a transaction.
func DeleteTrigger(txn *Txn, funcName string, verOrAlias string, triggerID string) error {
	// trigger
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	info, err := GetTriggerInfo(txn, funcName, verOrAlias, triggerID)
	if err != nil {
		if err != errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("failed to get trigger info, error: %s", err.Error())
		}
		return err
	}

	if err := txn.txn.TriggerDelete(genTriggerKey(t, funcName, verOrAlias, triggerID)); err != nil {
		log.GetLogger().Errorf("failed to delete trigger info, error: %s", err.Error())
		return err
	}

	// function-trigger
	if err := txn.txn.TriggerFunctionIndexDelete(genTriggerFunctionIndexKey(t, triggerID)); err != nil {
		log.GetLogger().Errorf("failed to delete trigger function index, error: %s", err.Error())
		return err
	}

	return info.EtcdSpec.delete(txn, funcName, verOrAlias)
}

// GetTriggerByFunctionNameVersion gets trigger entries by specified function name and version in a transaction.
func GetTriggerByFunctionNameVersion(txn Transaction, funcName string, funcVer string) ([]TriggerValue, error) {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}

	tKey := genTriggerKey(t, funcName, funcVer, "")
	tuples, err := txn.GetTxn().TriggerGetRange(tKey)
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger list, error: %s", err.Error())
		return nil, err
	}

	triggers := make([]TriggerValue, len(tuples), len(tuples))
	for i, tuple := range tuples {
		triggers[i] = tuple.Value
	}
	return triggers, nil
}

// GetFunctionInfoByTriggerID get function info by trigger id from etcd
func GetFunctionInfoByTriggerID(ctx server.Context, triggerID string) (TriggerFunctionIndexValue, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return TriggerFunctionIndexValue{}, err
	}
	tKey := genTriggerFunctionIndexKey(t, triggerID)
	res, err := db.TriggerFunctionIndexGet(ctx.Context(), tKey)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return TriggerFunctionIndexValue{}, errmsg.New(errmsg.TriggerNotFound, triggerID)
		}
		log.GetLogger().Errorf("failed to get trigger function index, error: %s", err.Error())
		return TriggerFunctionIndexValue{}, err
	}
	return res, nil
}

// GetTriggersByFunctionName get function trigger info from etcd by function name
func GetTriggersByFunctionName(txn *Txn, funcName string) ([]TriggerTuple, error) {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return []TriggerTuple{}, err
	}

	tuples, err := txn.txn.TriggerGetRange(genTriggerKey(t, funcName, "", ""))
	if err != nil {
		log.GetLogger().Debugf("failed to get trigger and function info, error: %s", err.Error())
		return []TriggerTuple{}, err
	}

	return tuples, nil
}

// GetTriggerInfoByTriggerID get trigger info by trigger id from etcd
func GetTriggerInfoByTriggerID(ctx server.Context, funcName string, verOrAlias string, triggerID string,
) (TriggerValue, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return TriggerValue{}, err
	}
	tKey := genTriggerKey(t, funcName, verOrAlias, triggerID)
	res, err := db.TriggerGet(ctx.Context(), tKey)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return TriggerValue{}, errmsg.New(errmsg.TriggerIDNotFound, triggerID)
		}
		log.GetLogger().Errorf("failed to get trigger info, error: %s", err.Error())
		return TriggerValue{}, err
	}
	return res, nil
}

// GetTriggerInfoList get trigger info list by function id from etcd
func GetTriggerInfoList(ctx server.Context, funcName string, verOrAlias string, pageIndex, pageSize int,
) ([]TriggerValue, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}
	tPrefix := genTriggerKey(t, funcName, verOrAlias, "")
	by := engine.SortBy{
		Order:  engine.Descend,
		Target: engine.SortModify,
	}
	triggerInfos, _, err := db.TriggerStream(ctx.Context(), tPrefix, by).ExecuteWithPage(pageIndex, pageSize)
	if err != nil {
		log.GetLogger().Debugf("failed to execute trigger stream with page, error: %s", err.Error())
		return nil, err
	}

	triggers := make([]TriggerValue, len(triggerInfos), len(triggerInfos))
	for i, triggerInfo := range triggerInfos {
		triggers[i] = triggerInfo.Value
	}
	return triggers, nil
}
