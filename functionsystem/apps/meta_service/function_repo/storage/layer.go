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
	"sort"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"

	"meta_service/common/engine"
	"meta_service/common/logger/log"
)

// NewLayerVersion says a new layer begins with version 1.
const NewLayerVersion = 1

func genLayerCountIndexKey(t server.TenantInfo, layerName string) LayerCountIndexKey {
	return LayerCountIndexKey{
		TenantInfo: t,
		LayerName:  layerName,
	}
}

func genLayerKey(t server.TenantInfo, layerName string, layerVersion int) LayerKey {
	return LayerKey{
		TenantInfo:   t,
		LayerName:    layerName,
		LayerVersion: layerVersion,
	}
}

// GetLayerLatestVersion gets layer's newest version. It also returns the newest version number.
func GetLayerLatestVersion(ctx server.Context, layerName string) (LayerValue, int, error) {
	tenant, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return LayerValue{}, 0, err
	}
	_, err = db.LayerCountIndexGet(ctx.Context(), genLayerCountIndexKey(tenant, layerName))
	if err != nil {
		log.GetLogger().Debugf("failed to get layer count index, error: %s", err.Error())
		return LayerValue{}, 0, err
	}
	k, v, err := db.LayerLastInRange(ctx.Context(), genLayerKey(tenant, layerName, 0))
	if err != nil {
		log.GetLogger().Debugf("failed to check layer last in range, error: %s", err.Error())
		return LayerValue{}, 0, err
	}
	return v, k.LayerVersion, nil
}

// GetLayerVersion gets a specific layer by its name and version
func GetLayerVersion(ctx server.Context, layerName string, layerVersion int) (LayerValue, error) {
	tenant, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return LayerValue{}, err
	}
	res, err := db.LayerGet(ctx.Context(), genLayerKey(tenant, layerName, layerVersion))
	if err != nil {
		log.GetLogger().Debugf("failed to get layer, error: %s", err.Error())
		return LayerValue{}, err
	}
	return res, nil
}

// GetLayerStream returns a range stream of layers
func GetLayerStream(ctx server.Context, layerName string) (*LayerPrepareStmt, error) {
	tenant, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}
	by := engine.SortBy{
		Order:  engine.Descend,
		Target: engine.SortModify,
	}
	return db.LayerStream(ctx.Context(), genLayerKey(tenant, layerName, 0), by), nil
}

// GetLayerSizeAndLatestVersion is the same as "GetLayerLatestVersion" but in transaction.
func GetLayerSizeAndLatestVersion(txn *Txn, layerName string) (int, int, error) {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return 0, 0, err
	}
	_, err = txn.txn.LayerCountIndexGet(genLayerCountIndexKey(tenant, layerName))
	if err != nil {
		log.GetLogger().Debugf("failed to get layer count index, error: %s", err.Error())
		return 0, 0, err
	}
	tuples, err := txn.txn.LayerGetRange(genLayerKey(tenant, layerName, 0))
	if err != nil {
		log.GetLogger().Errorf("failed to get layers from range, error: %s", err.Error())
		return 0, 0, err
	}
	if len(tuples) == 0 {
		return 0, 0, errmsg.KeyNotFoundError
	}
	sort.Slice(tuples, func(i, j int) bool {
		return tuples[i].Key.LayerVersion > tuples[j].Key.LayerVersion
	})
	return len(tuples), tuples[0].Key.LayerVersion, nil
}

// CountLayerTx returns the number of all layers.
func CountLayerTx(txn *Txn) (int, error) {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return 0, err
	}
	tuples, err := txn.txn.LayerCountIndexGetRange(genLayerCountIndexKey(tenant, ""))
	if err != nil {
		log.GetLogger().Errorf("failed to get layer count indexes from range, error: %s", err.Error())
		return 0, err
	}
	return len(tuples), nil
}

// GetLayerVersionTx is the same as "GetLayerVersion" but in transaction.
func GetLayerVersionTx(txn Transaction, layerName string, layerVersion int) (LayerValue, error) {
	tenant, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return LayerValue{}, err
	}
	res, err := txn.GetTxn().LayerGet(genLayerKey(tenant, layerName, layerVersion))
	if err != nil {
		log.GetLogger().Debugf("failed to get layer, error: %s", err.Error())
		return LayerValue{}, err
	}
	return res, nil
}

// GetLayerTx retrieves all layer versions of a layer.
func GetLayerTx(txn *Txn, layerName string) ([]LayerTuple, error) {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return nil, err
	}
	tuples, err := txn.txn.LayerGetRange(genLayerKey(tenant, layerName, 0))
	if err != nil {
		log.GetLogger().Errorf("failed to get layers from range, error: %s", err.Error())
		return nil, err
	}
	if len(tuples) == 0 {
		return nil, errmsg.New(errmsg.LayerNotFound, layerName)
	}
	return tuples, nil
}

// CreateLayer creates a specific layer version.
func CreateLayer(txn *Txn, layerName string, layerVersion int, layer LayerValue) error {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if layerVersion == NewLayerVersion {
		if err := txn.txn.LayerCountIndexPut(genLayerCountIndexKey(tenant, layerName),
			LayerCountIndexValue{}); err != nil {
			log.GetLogger().Errorf("failed to put layer count index, error: %s", err.Error())
			return err
		}
	}

	if err := txn.txn.LayerPut(genLayerKey(tenant, layerName, layerVersion), layer); err != nil {
		log.GetLogger().Errorf("failed to put layer, error: %s", err.Error())
		return err
	}
	return nil
}

// UpdateLayer updates a specific layer version.
func UpdateLayer(txn *Txn, layerName string, layerVersion int, layer LayerValue) error {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.txn.LayerPut(genLayerKey(tenant, layerName, layerVersion), layer); err != nil {
		log.GetLogger().Errorf("failed to put layer, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteLayer deletes all layer versions.
func DeleteLayer(txn *Txn, layerName string) error {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.txn.LayerDeleteRange(genLayerKey(tenant, layerName, 0)); err != nil {
		log.GetLogger().Errorf("failed to delete layers from range, error: %s", err.Error())
		return err
	}
	if err := txn.txn.LayerCountIndexDelete(genLayerCountIndexKey(tenant, layerName)); err != nil {
		log.GetLogger().Errorf("failed to delete layer count index, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteLayerVersion deletes a specific layer version.
func DeleteLayerVersion(txn *Txn, layerName string, layerVersion int) error {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := txn.txn.LayerDelete(genLayerKey(tenant, layerName, layerVersion)); err != nil {
		log.GetLogger().Errorf("failed to delete layer, error: %s", err.Error())
		return err
	}
	tuples, err := txn.txn.LayerGetRange(genLayerKey(tenant, layerName, 0))
	if err != nil {
		log.GetLogger().Errorf("failed to get layers from range, error: %s", err.Error())
		return err
	}
	if len(tuples) == 0 {
		if err := txn.txn.LayerCountIndexDelete(genLayerCountIndexKey(tenant, layerName)); err != nil {
			log.GetLogger().Errorf("failed to delete layer count index, error: %s", err.Error())
			return err
		}
	}
	return nil
}

// IsLayerUsed returns the first used layer version, 0 if layer is not being used.
func IsLayerUsed(txn *Txn, layerName string) (int, error) {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return 0, err
	}

	prefix := genLayerFunctionIndexKey(tenant, layerName, 0, "", "")
	tuples, err := txn.txn.LayerFunctionIndexGetRange(prefix)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer function indexes from range, error: %s", err.Error())
		return 0, err
	}

	if len(tuples) == 0 {
		return 0, nil
	}

	return tuples[0].Key.LayerVersion, nil
}

// IsLayerVersionUsed returns true if the specific layer version is being used.
func IsLayerVersionUsed(txn *Txn, layerName string, layerVersion int) (bool, error) {
	tenant, err := txn.c.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return false, err
	}

	prefix := genLayerFunctionIndexKey(tenant, layerName, layerVersion, "", "")
	tuples, err := txn.txn.LayerFunctionIndexGetRange(prefix)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer function indexes from range, error: %s", err.Error())
		return false, err
	}
	return len(tuples) > 0, nil
}
