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
	"strings"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils"

	"meta_service/common/constants"
	"meta_service/common/engine"
	"meta_service/common/logger/log"
)

var EngineSortBy = engine.SortBy{
	Order:  engine.Descend,
	Target: engine.SortModify,
}

// GenFunctionVersionKey Gen Function Version Key
func GenFunctionVersionKey(t server.TenantInfo, funcName string, funcVer string) FunctionVersionKey {
	return FunctionVersionKey{
		TenantInfo:      t,
		FunctionName:    funcName,
		FunctionVersion: funcVer,
	}
}

func genFunctionStatusKey(t server.TenantInfo, funcName string, funcVer string) FunctionStatusKey {
	var funcStatus FunctionStatusKey
	funcVersion := FunctionVersionKey{
		TenantInfo:      t,
		FunctionName:    funcName,
		FunctionVersion: funcVer,
	}

	funcStatus.FunctionVersionKey = funcVersion
	return funcStatus
}

func genLayerFunctionIndexKey(
	t server.TenantInfo, layerName string, layerVer int, funcName, funcVer string,
) LayerFunctionIndexKey {
	return LayerFunctionIndexKey{
		TenantInfo:      t,
		LayerName:       layerName,
		LayerVersion:    layerVer,
		FunctionName:    funcName,
		FunctionVersion: funcVer,
	}
}

func genObjectRefIndexKey(t server.TenantInfo, bucketID string, objectID string) ObjectRefIndexKey {
	return ObjectRefIndexKey{
		TenantInfo: t,
		BucketID:   bucketID,
		ObjectID:   objectID,
	}
}

// DeleteFunctionVersions deletes all function versions under a function name.
func DeleteFunctionVersions(txn Transaction, funcName string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	prefix := GenFunctionVersionKey(t, funcName, "")
	tuples, err := txn.GetTxn().FunctionVersionGetRange(prefix)
	if err != nil {
		log.GetLogger().Errorf("failed to get function version from range, error: %s", err.Error())
		return err
	}

	for _, tuple := range tuples {
		for _, layer := range tuple.Value.FunctionLayer {
			k := genLayerFunctionIndexKey(
				t, layer.Name, layer.Version, tuple.Value.Function.Name, tuple.Value.FunctionVersion.Version)
			if err := txn.GetTxn().LayerFunctionIndexDelete(k); err != nil {
				log.GetLogger().Errorf("failed to delete layer function index, error: %s", err.Error())
				return err
			}
		}
		if tuple.Value.FunctionVersion.Package.CodeUploadType != constants.S3StorageType {
			continue
		}
		if err := delObjRefCntTx(txn, tuple.Value.FunctionVersion.Package); err != nil {
			log.GetLogger().Errorf("failed to delete object reference count, error: %s", err.Error())
			return err
		}
	}

	if err := txn.GetTxn().FunctionVersionDeleteRange(prefix); err != nil {
		log.GetLogger().Errorf("failed to delete function version from range, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteFunctionStatuses deletes all function statuses under a function name.
func DeleteFunctionStatuses(txn Transaction, funcName string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	prefix := genFunctionStatusKey(t, funcName, "")
	if err := txn.GetTxn().FunctionStatusDeleteRange(prefix); err != nil {
		log.GetLogger().Errorf("failed to delete function status from range, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteFunctionVersion deletes function version entries by function name and version,
// related layer function version index entries also will be deleted.
func DeleteFunctionVersion(txn Transaction, funcName string, funcVer string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	key := GenFunctionVersionKey(t, funcName, funcVer)
	val, err := txn.GetTxn().FunctionVersionGet(key)
	if err != nil {
		log.GetLogger().Debugf("failed to get function version, error: %s", err.Error())
		return err
	}

	for _, layer := range val.FunctionLayer {
		k := genLayerFunctionIndexKey(t, layer.Name, layer.Version, val.Function.Name, val.FunctionVersion.Version)
		if err := txn.GetTxn().LayerFunctionIndexDelete(k); err != nil {
			log.GetLogger().Errorf("failed to delete layer function index, error: %s", err.Error())
			return err
		}
	}

	if err := delObjRefCntTx(txn, val.FunctionVersion.Package); err != nil {
		log.GetLogger().Errorf("failed to delete object reference count, error: %s", err.Error())
		return err
	}
	if err := txn.GetTxn().FunctionVersionDelete(key); err != nil {
		log.GetLogger().Errorf("failed to delete function version, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteFunctionStatus deletes all function status under a function name.
func DeleteFunctionStatus(txn Transaction, funcName string, funcVer string) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	key := genFunctionStatusKey(t, funcName, funcVer)
	if err := txn.GetTxn().FunctionStatusDelete(key); err != nil {
		log.GetLogger().Errorf("failed to delete function status, error: %s", err.Error())
		return err
	}
	return nil
}

// GetFunctionVersion reads function version entry by function name and version from storage within a txn.
func GetFunctionVersion(txn Transaction, funcName, funcVer string) (FunctionVersionValue, error) {
	if funcVer == "" {
		funcVer = utils.GetDefaultVersion()
	}
	t, err := txn.GetCtx().TenantInfo()
	var res FunctionVersionValue
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return FunctionVersionValue{}, err
	}
	res, err = txn.GetTxn().FunctionVersionGet(GenFunctionVersionKey(t, funcName, funcVer))
	if err != nil {
		log.GetLogger().Debugf("failed to get function version, error: %s", err.Error())
		return FunctionVersionValue{}, err
	}
	return res, nil
}

// GetFunctionVersions reads function version entries by function name from storage within a txn.
func GetFunctionVersions(txn Transaction, funcName string) ([]FunctionVersionValue, error) {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}

	tuples, err := txn.GetTxn().FunctionVersionGetRange(GenFunctionVersionKey(t, funcName, ""))
	if err != nil {
		log.GetLogger().Errorf("failed to get function version from range, error: %s", err.Error())
		return nil, err
	}
	if len(tuples) == 0 {
		return nil, errmsg.New(errmsg.FunctionNotFound, utils.RemoveServiceID(funcName))
	}

	fvs := make([]FunctionVersionValue, len(tuples), len(tuples))
	for i, tuple := range tuples {
		fvs[i] = tuple.Value
	}
	return fvs, nil
}

// CreateFunctionVersion saves function version entry to storage.
// It also saves 2 indexes: layer function version index and object reference count index.
func CreateFunctionVersion(txn Transaction, fv FunctionVersionValue) error {
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.GetTxn().FunctionVersionPut(
		GenFunctionVersionKey(t, fv.Function.Name, fv.FunctionVersion.Version), fv); err != nil {
		log.GetLogger().Errorf("failed to put function version, error: %s", err.Error())
		return err
	}

	for _, layer := range fv.FunctionLayer {
		k := genLayerFunctionIndexKey(t, layer.Name, layer.Version, fv.Function.Name, fv.FunctionVersion.Version)
		if err := txn.GetTxn().LayerFunctionIndexPut(k, LayerFunctionIndexValue{}); err != nil {
			log.GetLogger().Errorf("failed to put layer function index, error: %s", err.Error())
			return err
		}
	}
	// if package is upload by repo, storage type will be s3
	if fv.FunctionVersion.Package.CodeUploadType != constants.S3StorageType {
		return nil
	}
	if err := addObjRefCntTx(txn, fv.FunctionVersion.Package); err != nil {
		log.GetLogger().Errorf("failed to add object reference count, error: %s", err.Error())
		return err
	}
	return nil
}

// CreateFunctionStatus saves function status entry to storage.
func CreateFunctionStatus(txn Transaction, funcName string, funcVer string) error {
	statusValue := FunctionStatusValue{
		Status: constants.FunctionStatusUnavailable,
	}

	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}

	if err := txn.GetTxn().FunctionStatusPut(
		genFunctionStatusKey(t, funcName, funcVer), statusValue); err != nil {
		log.GetLogger().Errorf("failed to put function status, error: %s", err.Error())
		return err
	}

	return nil
}

// UpdateFunctionVersion updates function version entry to storage.
// It also updates 2 indexes: layer function version index and object reference count index.
func UpdateFunctionVersion(txn Transaction, fv FunctionVersionValue) error {
	if err := DeleteFunctionVersion(txn, fv.Function.Name, fv.FunctionVersion.Version); err != nil {
		if err != errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("failed to delete function version, error: %s", err.Error())
		}
		return err
	}

	if err := CreateFunctionVersion(txn, fv); err != nil {
		log.GetLogger().Errorf("failed to create function version, error: %s", err.Error())
		return err
	}
	return nil
}

func addObjRefCntTx(txn Transaction, pkg Package) error {
	if pkg.BucketID == "" || pkg.ObjectID == "" {
		return nil
	}

	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	key := genObjectRefIndexKey(t, pkg.BucketID, pkg.ObjectID)
	val, err := txn.GetTxn().ObjectRefIndexGet(key)
	if err != nil {
		if err != errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("failed to get object reference index, error: %s", err.Error())
			return err
		}
	}
	val.RefCnt++
	if err := txn.GetTxn().ObjectRefIndexPut(key, val); err != nil {
		log.GetLogger().Errorf("failed to put object reference index, error: %s", err.Error())
		return err
	}
	return nil
}

func delObjRefCntTx(txn Transaction, pkg Package) error {
	if pkg.CodeUploadType != constants.S3StorageType {
		return nil
	}
	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	key := genObjectRefIndexKey(t, pkg.BucketID, pkg.ObjectID)
	val, err := txn.GetTxn().ObjectRefIndexGet(key)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil
		}
		log.GetLogger().Errorf("failed to get object reference index, error: %s", err.Error())
		return err
	}
	val.RefCnt--
	if val.RefCnt <= 0 {
		if err := txn.GetTxn().ObjectRefIndexDelete(key); err != nil {
			log.GetLogger().Errorf("failed to delete object reference index, error: %s", err.Error())
			return err
		}
		return nil
	}
	if err := txn.GetTxn().ObjectRefIndexPut(key, val); err != nil {
		log.GetLogger().Errorf("failed to put object reference index, error: %s", err.Error())
		return err
	}
	return nil
}

// IsObjectReferred returns whether an object is referred by other function version.
// This method should be called only in upload function package transaction.
func IsObjectReferred(txn Transaction, bucketID string, objectID string) (bool, error) {
	if bucketID == "" || objectID == "" {
		return false, nil
	}

	t, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return false, err
	}
	val, err := txn.GetTxn().ObjectRefIndexGet(genObjectRefIndexKey(t, bucketID, objectID))
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return false, nil
		}
		log.GetLogger().Errorf("failed to get object reference index, error: %s", err.Error())
		return false, err
	}
	return val.RefCnt > 0, nil
}

// GetFunctionVersionSizeByName returns size of function version entry by specified function name.
func GetFunctionVersionSizeByName(ctx server.Context, funcName string) (int64, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return 0, err
	}

	res, err := db.FunctionVersionCount(ctx.Context(), GenFunctionVersionKey(t, funcName, ""))
	if err != nil {
		log.GetLogger().Errorf("failed to count function version, error: %s", err.Error())
		return 0, err
	}
	return res, nil
}

// GetFunctionList returns a function version entry list by a fuzzy function name.
// Returned list is only the page of specified page index and size.
func GetFunctionList(
	ctx server.Context, funcName string, funcVer string, kind string, pageIndex int, pageSize int,
) ([]FunctionVersionTuple, int, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, 0, err
	}
	stream := db.FunctionVersionStream(ctx.Context(), GenFunctionVersionKey(t, "", ""), EngineSortBy)
	if kind == constants.Faas {
		stream = metaDB.FunctionVersionStream(ctx.Context(), GenFunctionVersionKey(t, "", ""), EngineSortBy)
	}

	if funcVer != "" || funcName != "" {
		stream = stream.Filter(func(key FunctionVersionKey, val FunctionVersionValue) bool {
			if funcVer != "" && key.FunctionVersion != funcVer {
				return false
			}
			if funcName != "" && !strings.Contains(val.Function.Name, funcName) {
				return false
			}
			return true
		})
	}

	tuples, total, err := stream.ExecuteWithPage(pageIndex, pageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, 0, nil
		}
		log.GetLogger().Errorf("failed to get function version stream with page, error: %s", err.Error())
		return nil, 0, err
	}
	return tuples, total, nil
}

// GetFunctionVersionList returns function version entry list by specified function name.
// Returned list is only the page of specified page index and size.
func GetFunctionVersionList(
	ctx server.Context, funcName string, funcVer string, pageIndex int, pageSize int,
) ([]FunctionVersionTuple, int, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, 0, err
	}
	tuples, total, err := db.
		FunctionVersionStream(ctx.Context(), GenFunctionVersionKey(t, funcName, funcVer), EngineSortBy).
		ExecuteWithPage(pageIndex, pageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, 0, nil
		}
		log.GetLogger().Errorf("failed to get function version stream with page, error: %s", err.Error())
		return nil, 0, err
	}

	return tuples, total, nil
}

// GetFunctionByFunctionNameAndVersion gets function version entry by function name and version.
// It returns snerror(4115) if entry does not exist.
func GetFunctionByFunctionNameAndVersion(ctx server.Context, name string,
	version string, kind string,
) (FunctionVersionValue, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return FunctionVersionValue{}, err
	}

	currentDB := GetDB(kind)
	val, err := currentDB.FunctionVersionGet(ctx.Context(), GenFunctionVersionKey(t, name, version))
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return FunctionVersionValue{}, errmsg.New(errmsg.FunctionNotFound, utils.RemoveServiceID(name))
		}
		log.GetLogger().Errorf("failed to get function version, error: %s", err.Error())
		return FunctionVersionValue{}, err
	}

	funcStatus, err := currentDB.FunctionStatusGet(ctx.Context(), genFunctionStatusKey(t, name, version))
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			val.FunctionVersion.Status = constants.FunctionStatusUnavailable
			return val, nil
		}
		log.GetLogger().Errorf("failed to get function version, error: %s", err.Error())
		return FunctionVersionValue{}, err
	}
	val.FunctionVersion.Status = funcStatus.Status
	val.FunctionVersion.InstanceNum = funcStatus.InstanceNum
	return val, nil
}

// GetFunctionByFunctionNameAndAlias returns the function version from the alias version index table.
func GetFunctionByFunctionNameAndAlias(c server.Context, funcName string, aliasName string) string {
	value, err := GetAliasValue(c, funcName, aliasName)
	if err != nil {
		return ""
	}
	return value.FunctionVersion
}

// GetDB get db
func GetDB(kind string) *generatedKV {
	if kind == constants.Faas {
		return metaDB
	}
	return db
}
