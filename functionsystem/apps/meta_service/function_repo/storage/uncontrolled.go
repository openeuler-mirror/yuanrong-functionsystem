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
	"context"
	"time"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"

	"meta_service/common/engine"
	"meta_service/common/logger/log"
)

func genUncontrolledObjectKey(t server.TenantInfo, bucketID, objectID string) UncontrolledObjectKey {
	return UncontrolledObjectKey{
		TenantInfo: t,
		BucketID:   bucketID,
		ObjectID:   objectID,
	}
}

// AddUncontrolled sets a function package or a layer package to an uncontrolled stage. A typical routine works as:
// 1. add a function package to uncontrolled stage
// 2. upload the function package
// 3. save function meta info
// 4. remove the function package from uncontrolled stage
// In cases where an unexpected exit occurs in the middle of 2 and 3, a CRON job can later pick up any "half-done"
// events and clean up function packages and layer packages that are no longer valid.
func AddUncontrolled(ctx server.Context, bucketID, objectID string) error {
	tenant, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	val := UncontrolledObjectValue{time.Now()}
	if err := db.UncontrolledObjectPut(
		ctx.Context(), genUncontrolledObjectKey(tenant, bucketID, objectID), val); err != nil {
		log.GetLogger().Errorf("failed to put uncontrolled object, error: %s", err.Error())
		return err
	}
	return nil
}

// AddUncontrolledTx is the transactional version of AddUncontrolled.
func AddUncontrolledTx(txn Transaction, bucketID, objectID string) error {
	tenant, err := txn.GetCtx().TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	val := UncontrolledObjectValue{time.Now()}
	if err := txn.GetTxn().UncontrolledObjectPut(
		genUncontrolledObjectKey(tenant, bucketID, objectID), val); err != nil {
		log.GetLogger().Errorf("failed to put uncontrolled object, error: %s", err.Error())
		return err
	}
	return nil
}

// RemoveUncontrolled works together with AddUncontrolled or AddUncontrolledTx to unset the uncontrolled stage of a
// function package or layer package.
func RemoveUncontrolled(ctx server.Context, bucketID, objectID string) error {
	tenant, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return err
	}
	if err := db.UncontrolledObjectDelete(
		ctx.Context(), genUncontrolledObjectKey(tenant, bucketID, objectID)); err != nil {
		log.GetLogger().Errorf("failed to delete uncontrolled object, error: %s", err.Error())
		return err
	}
	return nil
}

// GetUncontrolledList ranges through all uncontrolled packages' records. A CRON job use this function to find
// packages that are no longer valid.
func GetUncontrolledList() ([]UncontrolledObjectTuple, error) {
	checkpoint := time.Now().Add(-5 * time.Minute)
	tuples, err := db.UncontrolledObjectStream(
		context.Background(), UncontrolledObjectKey{}, engine.SortBy{}).Filter(
		func(key UncontrolledObjectKey, val UncontrolledObjectValue) bool {
			// We only care about stale objects.
			return val.CreateTime.Before(checkpoint)
		}).Execute()
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, nil
		}
		log.GetLogger().Errorf("failed to get uncontrolled stream, error: %s", err.Error())
		return nil, err
	}
	return tuples, nil
}

// RemoveUncontrolledByKey works similar to RemoveUncontrolled except that it directly takes an UncontrolledObjectKey
// as input and uses context.Background. A CRON job use this function after the deletion of an invalid package.
func RemoveUncontrolledByKey(key UncontrolledObjectKey) error {
	if err := db.UncontrolledObjectDelete(context.Background(), key); err != nil {
		log.GetLogger().Errorf("failed to remove uncontrolled object, error: %s", err.Error())
		return err
	}
	return nil
}
