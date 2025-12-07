/*
 * Copyright (c) 2025 Huawei Technologies Co., Ltd
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
	"meta_service/common/logger/log"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/utils/constants"
)

func validateInstanceConfig(ctx server.Context, insConfig model.InstanceConfig, info model.ReserveInsBaseInfo,
	isUpdate bool,
) error {
	maxReservedInstanceLabels := int64(config.RepoCfg.FunctionCfg.InstanceLabelMax)
	if insConfig.MaxInstance < insConfig.MinInstance {
		return errmsg.NewParamError("maxInstance %d is less than minInstance %d")
	}
	if isUpdate {
		_, getErr := storage.GetReserveInstanceConfig(ctx, insConfig.ClusterID, info)
		if getErr != nil {
			return errmsg.NewParamError("failed to find reserve instance config for update")
		}
	} else {
		cnt, err := storage.CountReserveInstanceLabels(ctx, insConfig.ClusterID, info)
		if err != nil {
			return err
		}
		if cnt >= maxReservedInstanceLabels {
			log.GetLogger().Errorf("label %s for function %s version %s, already reach %d", info.InstanceLabel,
				info.FuncName, info.Version, maxReservedInstanceLabels)
			return errmsg.NewParamError("label count reached max(%d)", maxReservedInstanceLabels)
		}
	}
	return nil
}

// CreateReserveInstance create reserve instance
func CreateReserveInstance(ctx server.Context, req model.CreateReserveInsRequest,
	isUpdate bool) (model.CreateReserveInsResponse,
	error,
) {
	txn := storage.GetTxnByKind(ctx, "faas")
	defer txn.Cancel()
	createResp := model.CreateReserveInsResponse{}
	createResp.Code = 0
	_, err := storage.GetFunctionByFunctionNameAndVersion(ctx, req.FuncName, req.Version, constants.Faas)
	if err != nil {
		log.GetLogger().Errorf("%s|failed to get function %s by version %s", req.TraceID, req.FuncName, req.Version)
		return createResp, err
	}
	info := model.ReserveInsBaseInfo{
		FuncName: req.FuncName,
		Version:  req.Version, TenantID: req.TenantID, InstanceLabel: req.InstanceLabel,
	}
	clusterSet := make(map[string]bool)
	for _, insConfig := range req.InstanceConfigInfos {
		if insConfig.ClusterID == "" {
			insConfig.ClusterID = constants.DefaultClusterID
		}
		if _, exists := config.RepoCfg.ClusterID[insConfig.ClusterID]; !exists {
			return createResp, errmsg.NewParamError("clusterID %s is not found", insConfig.ClusterID)
		}
		if _, ok := clusterSet[insConfig.ClusterID]; ok {
			return createResp, errmsg.NewParamError("clusterID %s is repeated", insConfig.ClusterID)
		} else {
			clusterSet[insConfig.ClusterID] = true
		}
		if err := validateInstanceConfig(ctx, insConfig, info, isUpdate); err != nil {
			return createResp, err
		}
		err = storage.CreateOrUpdateReserveInstanceConfig(txn, info, insConfig)
		if err != nil {
			log.GetLogger().Errorf("%s|failed to create reserve instance,cluster:%s,functionName:%s,version:%s, "+
				"err:%s", req.TraceID, insConfig.ClusterID, info.FuncName, info.Version, err.Error())
			return createResp, err
		}
	}
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to create reserve instance when committing: %s", err.Error())
		return createResp, err
	}
	var instanceConfigs []model.InstanceConfig
	for cluster := range config.RepoCfg.ClusterID {
		instanceConfig, err := storage.GetReserveInstanceConfig(ctx, cluster, info)
		if err != nil {
			log.GetLogger().Warnf("%s|failed to get instance config cluster:%s", req.TraceID, cluster)
		} else {
			instanceConfigs = append(instanceConfigs, instanceConfig)
		}
	}
	createResp.ReserveInsBaseInfo = info
	createResp.InstanceConfigInfos = instanceConfigs
	return createResp, nil
}

// DeleteReserveInstance delete reserve instance
func DeleteReserveInstance(ctx server.Context, req model.DeleteReserveInsRequest) (model.DeleteReserveInsResponse,
	error,
) {
	txn := storage.GetTxnByKind(ctx, constants.Faas)
	defer txn.Cancel()
	info := model.ReserveInsBaseInfo{
		FuncName: req.FuncName,
		Version:  req.Version, TenantID: req.TenantID, InstanceLabel: req.InstanceLabel,
	}
	_, err := storage.GetFunctionByFunctionNameAndVersion(ctx, req.FuncName, req.Version, constants.Faas)
	if err != nil {
		log.GetLogger().Errorf("%s|failed to get function %s by version %s", req.TraceID, req.FuncName, req.Version)
		return model.DeleteReserveInsResponse{}, err
	}
	for cluster := range config.RepoCfg.ClusterID {
		storage.DeleteReserveInstanceConfig(txn, cluster, info)
	}
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to delete reserve instance when committing: %s", err.Error())
		return model.DeleteReserveInsResponse{}, err
	}
	return model.DeleteReserveInsResponse{}, nil
}

// GetReserveInstanceConfigs get reserved instance config
func GetReserveInstanceConfigs(ctx server.Context, req model.GetReserveInsRequest) (model.GetReserveInsResponse,
	error,
) {
	txn := storage.GetTxnByKind(ctx, constants.Faas)
	defer txn.Cancel()
	info := model.ReserveInsBaseInfo{
		FuncName: req.FuncName, Version: req.Version, TenantID: req.TenantID,
		InstanceLabel: "",
	}
	_, err := storage.GetFunctionByFunctionNameAndVersion(ctx, req.FuncName, req.Version, constants.Faas)
	if err != nil {
		log.GetLogger().Errorf("failed to get function %s by version %s", req.FuncName, req.Version)
		return model.GetReserveInsResponse{}, err
	}
	resp := model.GetReserveInsResponse{}
	resp.GetReserveInsResults = make([]model.GetReserveInsResult, 0)
	labelIndex := make(map[string]int)
	cnt := 0
	for cluster := range config.RepoCfg.ClusterID {
		results, err := storage.GetReserveInstanceConfigList(ctx, cluster, info)
		if err != nil {
			return model.GetReserveInsResponse{}, err
		}
		for _, val := range results {
			index, ok := labelIndex[val.InstanceLabel]
			if ok {
				resp.GetReserveInsResults[index].InstanceConfigInfos = append(resp.GetReserveInsResults[index].
					InstanceConfigInfos, val.InstanceConfig)
			} else {
				insRes := model.GetReserveInsResult{
					InstanceLabel:       val.InstanceLabel,
					InstanceConfigInfos: make([]model.InstanceConfig, 0),
				}
				insRes.InstanceConfigInfos = append(insRes.InstanceConfigInfos, val.InstanceConfig)
				resp.GetReserveInsResults = append(resp.GetReserveInsResults, insRes)
				labelIndex[val.InstanceLabel] = cnt
				cnt++
			}
		}
	}
	resp.Total = len(resp.GetReserveInsResults)
	return resp, nil
}
