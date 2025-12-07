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

// Package service is processing service codes
package service

import (
	"errors"
	"fmt"
	"regexp"

	"meta_service/common/logger/log"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/utils/constants"
)

const (
	idRegex            = "^[a-z0-9]([-a-z0-9]{0,38}[a-z0-9])?$"
	groupRegex         = "^[a-zA-Z0-9]([-a-zA-Z0-9]{0,38}[a-zA-Z0-9])?$"
	minSize            = 0
	maxImageLen        = 200
	maxInitImageLen    = 200
	maxRuntimeClassLen = 64
	defaultPageSize    = 10
	defaultPageIndex   = 1
	minIdleTime        = -1
)

var (
	errIDFormat = errors.New("pod pool id can contain only lowercase letters, digits and '-'. " +
		"it cannot start or end with '-' and cannot exceed 40 characters or less than 1 characters")
	errGroupFormat = errors.New("pod pool group can contain only letters, digits and '-'. " +
		"it cannot start or end with '-' and cannot exceed 40 characters")
)

// CreatePodPool create pod pool
func CreatePodPool(ctx server.Context, req model.PodPoolCreateRequest) (model.PodPoolCreateResponse, error) {
	if len(req.Pools) == 0 {
		return model.PodPoolCreateResponse{}, errors.New("1failed to create pod pool, err: input pod pool list is empty")
	}
	var res model.PodPoolCreateResponse
	var returnErrMsg string
	for _, pool := range req.Pools {
		if err := createPodPool(ctx, pool); err != nil {
			res.FailedPools = append(res.FailedPools, pool.Id)
			returnErrMsg = joinErrMsg(returnErrMsg, err)
		}
	}
	if returnErrMsg != "" {
		return res, errors.New(returnErrMsg)
	}
	return res, nil
}

func createPodPool(ctx server.Context, pool model.Pool) error {
	if err := validatePodPoolCreateParams(&pool); err != nil {
		return err
	}
	txn := storage.GetTxnByKind(ctx, "")
	defer txn.Cancel()
	info, err := storage.GetPodPool(txn, pool.Id)
	if err == nil || info.Status == constants.Deleted {
		errMsg := fmt.Sprintf("failed to create pod pool, id: %s,"+
			"err: pool id already exists or is deleting", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	poolVal := convertPoolModelToPoolValue(pool)
	err = storage.CreateUpdatePodPool(txn, poolVal)
	if err != nil {
		return err
	}
	if err = txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit transaction: %s", err.Error())
		return err
	}
	return nil
}

func joinErrMsg(errMsg string, err error) string {
	if errMsg == "" {
		return err.Error()
	}
	return errMsg + ";" + err.Error()
}

func validatePodPoolCreateParams(pool *model.Pool) error {
	var errMsg string
	if pool == nil {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: pod pool is empty", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	isMatch, err := regexp.MatchString(idRegex, pool.Id)
	if err != nil {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: %s", pool.Id, err.Error())
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if !isMatch {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: %s", pool.Id, errIDFormat.Error())
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.Group == "" {
		pool.Group = "default"
	}
	isMatch, err = regexp.MatchString(groupRegex, pool.Group)
	if err != nil {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, group: %s, err: %s",
			pool.Id, pool.Group, err.Error())
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if !isMatch {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, group: %s, err: %s",
			pool.Id, pool.Group, errGroupFormat.Error())
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	return validatePodPoolCreateParamsLength(pool)
}

func validatePodPoolCreateParamsLength(pool *model.Pool) error {
	var errMsg string
	if pool.Size < minSize {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: size is less than 0", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.Size != 0 && pool.MaxSize == 0 {
		pool.MaxSize = pool.Size
	}
	if pool.MaxSize < pool.Size {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: max_size is less than size", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.MaxSize > pool.Size && len(pool.HorizontalPodAutoscalerSpec) > 0 {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, "+
			"err: max_size greater than size and horizontal_pod_autoscaler_spec cannot be set at the same time",
			pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.IdleRecycleTime.Reserved < minIdleTime {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: idle time of reserved should not less than %d",
			pool.Id, minIdleTime)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.IdleRecycleTime.Scaled < minIdleTime {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: idle time of scaled should not less than %d",
			pool.Id, minIdleTime)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if len(pool.Image) > maxImageLen {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: image len is not in [1, 200]", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if len(pool.InitImage) > maxInitImageLen {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: init image len is not in [1, 200]", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if len(pool.RuntimeClassName) > maxRuntimeClassLen {
		errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: runtime_class_name should not greater than %d",
			pool.Id, maxRuntimeClassLen)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.PodPendingDurationThreshold < 0 {
        errMsg = fmt.Sprintf("failed to create pod pool, id: %s, err: podPendingDurationThreshold should not be less"+
            " than 0, which is %d", pool.Id, pool.PodPendingDurationThreshold)
        log.GetLogger().Errorf(errMsg)
        return errors.New(errMsg)
    }
	return nil
}

// DeletePodPool delete pod pool
func DeletePodPool(ctx server.Context, id, group string) error {
	if id == "" && group == "" {
		return errors.New("id and group cannot be empty at the same time")
	}
	// start transaction
	txn := storage.GetTxnByKind(ctx, "")
	defer txn.Cancel()
	// query all without page info
	pools, _, err := storage.GetPodPoolList(ctx, id, group, -1, -1)
	if err != nil {
		log.GetLogger().Errorf("failed to get pod pool to delete, id: %s, group: %s, err: %s", id, group, err.Error())
		return err
	}
	if len(pools) == 0 {
		return errmsg.PoolNotFoundError
	}
	for _, pool := range pools {
		err = deletePodPool(txn, pool.Value)
		if err != nil {
			log.GetLogger().Errorf("failed to delete pod pool, id: %s, err: %s", pool.Value.Id, err.Error())
			return err
		}
	}
	if err = txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit transaction: %s", err.Error())
		return err
	}
	return nil
}

func deletePodPool(txn storage.Transaction, pool storage.PoolValue) error {
	pool.Status = constants.Deleted
	if err := storage.CreateUpdatePodPool(txn, pool); err != nil {
		return err
	}
	return nil
}

// UpdatePodPool update pod pool info
func UpdatePodPool(ctx server.Context, req model.PodPoolUpdateRequest) error {
	if req.Size < minSize {
		errMsg := fmt.Sprintf("failed to update pod pool, err: size is less than 0")
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if req.MaxSize < minSize {
		errMsg := fmt.Sprintf("failed to update pod pool, err: max_size is less than 0")
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	// start transaction
	txn := storage.GetTxnByKind(ctx, "")
	defer txn.Cancel()
	pool, err := storage.GetPodPool(txn, req.ID)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return errmsg.PoolNotFoundError
		}
		return err
	}
	if pool.Status == constants.Deleted {
		errMsg := fmt.Sprintf("the pod pool has already deleted, id: %s", pool.Id)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	} else if pool.Status == constants.New || pool.Status == constants.Update {
		errMsg := fmt.Sprintf("pool status is %d , not allowed to update", pool.Status)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if !pool.Scalable {
		// not scalable, ignore max_size
		req.MaxSize = req.Size
	} else {
		// if max size is 0, but size is not default, set max_size to old
		if req.MaxSize == 0 && req.Size != 0 {
			req.MaxSize = pool.MaxSize
		}
	}
	pool.Size = req.Size
	pool.MaxSize = req.MaxSize
	pool.Status = constants.Update
	if len(req.HorizontalPodAutoscalerSpec) != 0 {
		pool.HorizontalPodAutoscalerSpec = req.HorizontalPodAutoscalerSpec
	}
	if req.MaxSize < req.Size {
		errMsg := fmt.Sprintf("failed to update pod pool, err: max_size is less than size")
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if pool.MaxSize > pool.Size && len(pool.HorizontalPodAutoscalerSpec) > 0 {
		errMsg := fmt.Sprintf("failed to update pod pool, " +
			"err: max_size greater than size and horizontal_pod_autoscaler_spec cannot be set at the same time")
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if err = storage.CreateUpdatePodPool(txn, pool); err != nil {
		return err
	}
	if err = txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit transaction: %s", err.Error())
		return err
	}
	return nil
}

// GetPodPool get pod pool info
func GetPodPool(ctx server.Context, req model.PodPoolGetRequest) (model.PodPoolGetResponse, error) {
	err := validatePodPoolGetParams(req)
	if err != nil {
		return model.PodPoolGetResponse{}, nil
	}
	resp := model.PodPoolGetResponse{}
	txn := storage.GetTxnByKind(ctx, "")
	defer txn.Cancel()
	pageIndex := req.Offset
	pageSize := req.Limit
	if pageIndex <= 0 {
		pageIndex = defaultPageIndex
	}
	if pageSize <= 0 {
		pageSize = defaultPageSize
	}
	tuples, total, err := storage.GetPodPoolList(ctx, req.ID, req.Group, pageIndex, pageSize)
	if err != nil {
		log.GetLogger().Errorf("failed to get pool list :%s", err.Error())
		return resp, nil
	}
	pools := make([]model.Pool, len(tuples), len(tuples))
	for i, tuple := range tuples {
		pools[i] = convertPoolValueToPoolModel(tuple.Value)
	}
	resp.PodPools = pools
	resp.Count = total
	return resp, nil
}

func convertPoolValueToPoolModel(val storage.PoolValue) model.Pool {
	return model.Pool{
		Id:                          val.Id,
		Group:                       val.Group,
		Size:                        val.Size,
		MaxSize:                     val.MaxSize,
		ReadyCount:                  val.ReadyCount,
		Status:                      val.Status,
		Msg:                         val.Msg,
		Image:                       val.Image,
		InitImage:                   val.InitImage,
		Reuse:                       val.Reuse,
		Labels:                      val.Labels,
		Environment:                 val.Environment,
		Resources:                   val.Resources,
		Volumes:                     val.Volumes,
		VolumeMounts:                val.VolumeMounts,
		RuntimeClassName:            val.RuntimeClassName,
		NodeSelector:                val.NodeSelector,
		Tolerations:                 val.Tolerations,
		Affinities:                  val.Affinities,
		HorizontalPodAutoscalerSpec: val.HorizontalPodAutoscalerSpec,
		TopologySpreadConstraints:   val.TopologySpreadConstraints,
		PodPendingDurationThreshold: val.PodPendingDurationThreshold,
		IdleRecycleTime:             val.IdleRecycleTime,
		Scalable:                    val.Scalable,
	}
}

func convertPoolModelToPoolValue(val model.Pool) storage.PoolValue {
	poolVal := storage.PoolValue{}
	poolVal.Id = val.Id
	poolVal.Group = val.Group
	poolVal.Size = val.Size
	poolVal.MaxSize = val.MaxSize
	poolVal.ReadyCount = val.ReadyCount
	poolVal.Status = val.Status
	poolVal.Msg = val.Msg
	poolVal.Image = val.Image
	poolVal.InitImage = val.InitImage
	poolVal.Reuse = val.Reuse
	poolVal.Labels = val.Labels
	poolVal.Environment = val.Environment
	poolVal.Resources = val.Resources
	poolVal.VolumeMounts = val.VolumeMounts
	poolVal.Volumes = val.Volumes
	poolVal.Affinities = val.Affinities
	poolVal.Tolerations = val.Tolerations
	poolVal.NodeSelector = val.NodeSelector
	poolVal.RuntimeClassName = val.RuntimeClassName
	poolVal.HorizontalPodAutoscalerSpec = val.HorizontalPodAutoscalerSpec
	poolVal.TopologySpreadConstraints = val.TopologySpreadConstraints
	poolVal.PodPendingDurationThreshold = val.PodPendingDurationThreshold
	poolVal.IdleRecycleTime = val.IdleRecycleTime
	if val.MaxSize > val.Size {
		poolVal.Scalable = true
	} else {
		poolVal.Scalable = false
	}
	return poolVal
}

func validatePodPoolGetParams(req model.PodPoolGetRequest) error {
	if req.Limit <= 0 {
		errMsg := fmt.Sprintf("pod pool page: %d should greater than 0", req.Limit)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	if req.Offset < 0 {
		errMsg := fmt.Sprintf("pod pool offset: %d should greater equal than 0", req.Offset)
		log.GetLogger().Errorf(errMsg)
		return errors.New(errMsg)
	}
	return nil
}
