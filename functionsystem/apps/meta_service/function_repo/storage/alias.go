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

// IsFuncVersionExist check if function version exist
func IsFuncVersionExist(txn *Txn, funcName, funcVersion string) (bool, error) {
	_, err := GetFunctionVersion(txn, funcName, funcVersion)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("failed to get function version, error: %s", err.Error())
		return false, err
	}
	return true, nil
}

// IsAliasNameExist check if alias name exist
func IsAliasNameExist(txn *Txn, fName, aName string) (bool, error) {
	_, err := GetAlias(txn, fName, aName)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("failed to get alias version, error: %s", err.Error())
		return false, err
	}
	return true, nil
}

func genAliasKey(t server.TenantInfo, funcName, aliasName string) AliasKey {
	return AliasKey{
		TenantInfo:   t,
		FunctionName: funcName,
		AliasName:    aliasName,
	}
}

func genAliasRoutingIndexKey(t server.TenantInfo, funcName, funcVersion string) AliasRoutingIndexKey {
	return AliasRoutingIndexKey{
		TenantInfo:      t,
		FunctionName:    funcName,
		FunctionVersion: funcVersion,
	}
}

// CreateAlias write data to alias table and alias routing index table on storage within a transaction
func CreateAlias(txn *Txn, alias AliasValue) error {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.txn.AliasPut(genAliasKey(t, alias.FunctionName, alias.Name), alias); err != nil {
		log.GetLogger().Errorf("alias failed to put entry, error: %s", err.Error())
		return err
	}

	for k := range alias.RoutingConfig {
		if err := txn.txn.AliasRoutingIndexPut(
			genAliasRoutingIndexKey(t, alias.FunctionName, k),
			AliasRoutingIndexValue{Name: alias.Name}); err != nil {

			log.GetLogger().Errorf("alias failed to put routing index, error: %s", err.Error())
			return err
		}
	}

	return nil
}

// DeleteAliasByFunctionName deletes alias metadata entry and related index entries within a transaction
func DeleteAliasByFunctionName(txn Transaction, funcName string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	if err := txn.GetTxn().AliasDeleteRange(genAliasKey(t, funcName, "")); err != nil {
		log.GetLogger().Errorf("alias failed to delete range, error: %s", err.Error())
		return err
	}

	if err := txn.GetTxn().AliasRoutingIndexDeleteRange(genAliasRoutingIndexKey(t, funcName, "")); err != nil {
		log.GetLogger().Errorf("alias failed to delete routing index range, error: %s", err.Error())
		return err
	}

	return nil
}

// DeleteAlias deletes alias metadata and related index entries by specified alias name
func DeleteAlias(txn *Txn, funcName string, aliasName string, routingVers []string) error {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.txn.AliasDelete(genAliasKey(t, funcName, aliasName)); err != nil {
		log.GetLogger().Errorf("alias failed to delete, error: %s", err.Error())
		return err
	}
	for _, v := range routingVers {
		if err := txn.txn.AliasRoutingIndexDelete(genAliasRoutingIndexKey(t, funcName, v)); err != nil {
			log.GetLogger().Errorf("alias failed to delete routing index, error: %s", err.Error())
			return err
		}
	}
	return nil
}

// GetAliasNumByFunctionName returns alias number of specified function name within a transaction
func GetAliasNumByFunctionName(txn Transaction, funcName string) (int, error) {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return 0, err
	}

	tuples, err := txn.GetTxn().AliasGetRange(genAliasKey(t, funcName, ""))
	if err != nil {
		log.GetLogger().Errorf("alias failed to get range, error: %s", err.Error())
		return 0, err
	}
	return len(tuples), nil
}

// AliasRoutingExist returns true if any alias routings is referred to specified function name and version
// within a transaction
func AliasRoutingExist(txn Transaction, funcName, funcVer string) (bool, error) {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return false, err
	}

	_, err = txn.GetTxn().AliasRoutingIndexGet(genAliasRoutingIndexKey(t, funcName, funcVer))
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("alias failed to get routing index, error: %s", err.Error())
		return false, err
	}
	return true, nil
}

// GetAlias get alias information from storage within a transaction
func GetAlias(txn *Txn, funcName, aliasName string) (AliasValue, error) {
	t, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return AliasValue{}, err
	}

	res, err := txn.txn.AliasGet(genAliasKey(t, funcName, aliasName))
	if err != nil {
		log.GetLogger().Debugf("failed to get alias, error: %s", err.Error())
		return AliasValue{}, err
	}
	return res, nil
}

// AliasNameExist returns true if alias can be found in alias version index entry.
func AliasNameExist(txn *Txn, funcName, aliasName string) (bool, error) {
	_, err := GetAlias(txn, funcName, aliasName)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("failed to get alias version index, error: %s", err.Error())
		return false, err
	}
	return true, nil
}

// GetAliasesByPage get aliases list in page range
func GetAliasesByPage(ctx server.Context, funcName string, pageIndex, pageSize int) ([]AliasValue, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}

	by := engine.SortBy{
		Order:  engine.Descend,
		Target: engine.SortModify,
	}
	tuples, _, err := db.AliasStream(ctx.Context(),
		genAliasKey(t, funcName, ""), by).ExecuteWithPage(pageIndex, pageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, nil
		}
		log.GetLogger().Errorf(" failed to get aliases from stream, error: %s", err.Error())
		return nil, err
	}
	res := make([]AliasValue, len(tuples), len(tuples))
	for i, tuple := range tuples {
		res[i] = tuple.Value
	}
	return res, nil
}

// GetAliasValue get alias value from storage
func GetAliasValue(c server.Context, funcName, aliasName string) (AliasValue, error) {
	t, err := c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return AliasValue{}, err
	}
	res, err := db.AliasGet(c.Context(), genAliasKey(t, funcName, aliasName))
	if err != nil {
		log.GetLogger().Debugf("alias get failed, error: %s", err.Error())
		return AliasValue{}, err
	}
	return res, nil
}
