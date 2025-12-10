/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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
	"meta_service/function_repo/utils/constants"

	"meta_service/common/logger/log"
)

func buildPodPoolRegisterKey(id string) string {
	// format: /yr/podpools/info/<id>/
	keys := []string{constants.PodPoolPrefix}
	keys = append(keys, id)
	return strings.Join(keys, constants.ETCDKeySeparator)
}

// DeletePodPool delete a pod pool.
func DeletePodPool(txn *Txn, id string) {
	key := buildPodPoolRegisterKey(id)
	txn.Delete(key)
	return
}

// GetPodPoolList get pool pool list by id/group
func GetPodPoolList(ctx server.Context, id string, group string, pageIndex int, pageSize int) ([]PoolTuple, int,
	error,
) {
	stream := db.PoolStream(ctx.Context(), PoolKey{ID: id}, EngineSortBy)
	if id != "" || group != "" {
		stream = stream.Filter(func(key PoolKey, val PoolValue) bool {
			if id != "" && val.Id != id {
				return false
			}
			if group != "" && val.Group != group {
				return false
			}
			return true
		})
	}
	var tuples []PoolTuple
	var err error
	total := 0
	if pageIndex >= 0 && pageSize >= 0 {
		tuples, total, err = stream.ExecuteWithPage(pageIndex, pageSize)
	} else {
		tuples, err = stream.Execute()
	}
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return nil, 0, nil
		}
		log.GetLogger().Errorf("failed to get pool stream with page, error: %s", err.Error())
		return nil, 0, err
	}
	return tuples, total, nil
}

// CreateUpdatePodPool create or update pod pool
func CreateUpdatePodPool(txn Transaction, value PoolValue) error {
	if err := txn.GetTxn().PoolPut(PoolKey{ID: value.Id}, value); err != nil {
		log.GetLogger().Errorf("failed to put pool info, error: %s", err.Error())
		return err
	}
	return nil
}

// GetPodPool get a pod pool.
func GetPodPool(txn Transaction, id string) (PoolValue, error) {
	res, err := txn.GetTxn().PoolGet(PoolKey{ID: id})
	if err != nil {
		log.GetLogger().Warnf("failed to get pod pool, error: %s", err.Error())
		return PoolValue{}, err
	}
	return res, nil
}
