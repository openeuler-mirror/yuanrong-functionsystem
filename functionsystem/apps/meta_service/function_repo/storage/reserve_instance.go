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

package storage

import (
	"encoding/json"
	"io"
	"strings"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils/constants"

	"meta_service/common/logger/log"
	"meta_service/common/metadata"
)

const labelKeyLen int = 14

func buildReserveInstanceWithLabelKey(clusterID, tenantID, name, funcVersion, label string) string {
	// format: /instances/business/yrk/cluster/cluster001/tenant/<tenantID>/function/<functionName>/version\
	// /<version>/label/<label>
	keys := []string{constants.InstancePrefix, constants.BusinessKey, "yrk", constants.ClusterKey}
	keys = append(keys, clusterID, constants.TenantKey, tenantID)
	keys = append(keys, constants.ResourceKey, name)
	keys = append(keys, constants.VersionKey, funcVersion)
	if label != "" {
		keys = append(keys, constants.LabelKey, label)
	} else {
		keys = append(keys, constants.LabelKey)
	}
	return strings.Join(keys, constants.ETCDKeySeparator)
}

func getLabelFromKey(key string) string {
	item := strings.Split(key, constants.ETCDKeySeparator)
	if len(item) == labelKeyLen {
		return item[labelKeyLen-1]
	}
	return ""
}

// CreateOrUpdateReserveInstanceConfig create or update reserve instance config
func CreateOrUpdateReserveInstanceConfig(txn Transaction, info model.ReserveInsBaseInfo,
	config model.InstanceConfig,
) error {
	key := buildReserveInstanceWithLabelKey(config.ClusterID, info.TenantID, info.FuncName, info.Version,
		info.InstanceLabel)
	instanceMeta := metadata.FaaSInstanceMetaData{}
	instanceMeta.MaxInstance = config.MaxInstance
	instanceMeta.MinInstance = config.MinInstance
	data := metadata.ReserveInstanceMetaData{
		InstanceMetaData: instanceMeta,
	}
	value, err := json.Marshal(data)
	if err != nil {
		log.GetLogger().Errorf("failed to marshal version value: %s", err.Error())
		return err
	}
	txn.Put(key, string(value))
	return nil
}

// GetReserveInstanceConfig get reserved instance config
func GetReserveInstanceConfig(ctx server.Context, clusterID string,
	info model.ReserveInsBaseInfo,
) (model.InstanceConfig, error) {
	key := buildReserveInstanceWithLabelKey(clusterID, info.TenantID, info.FuncName, info.Version, info.InstanceLabel)
	val, err := db.eng.Get(ctx.Context(), key)
	if err != nil {
		log.GetLogger().Errorf("failed to get reserve instance config from etcd, key:%s, err:%s", key, err)
		return model.InstanceConfig{}, err
	}
	data := metadata.ReserveInstanceMetaData{}
	err = json.Unmarshal([]byte(val), &data)
	if err != nil {
		log.GetLogger().Errorf("failed to unmarshal instance config info, err: %s", err)
		return model.InstanceConfig{}, err
	}
	instanceCong := model.InstanceConfig{}
	instanceCong.MaxInstance = data.InstanceMetaData.MaxInstance
	instanceCong.MinInstance = data.InstanceMetaData.MinInstance
	instanceCong.ClusterID = clusterID
	return instanceCong, nil
}

// DeleteReserveInstanceConfig delete reserved instance config
func DeleteReserveInstanceConfig(txn Transaction, clusterID string, info model.ReserveInsBaseInfo) {
	key := buildReserveInstanceWithLabelKey(clusterID, info.TenantID, info.FuncName, info.Version, info.InstanceLabel)
	txn.DeleteRange(key)
}

// CountReserveInstanceLabels -
func CountReserveInstanceLabels(ctx server.Context, clusterID string,
	info model.ReserveInsBaseInfo,
) (int64, error) {
	keyPrefix := buildReserveInstanceWithLabelKey(clusterID, info.TenantID, info.FuncName, info.Version, "")
	keyPrefix = keyPrefix + constants.ETCDKeySeparator + constants.LabelKey
	cnt, err := db.eng.Count(ctx.Context(), keyPrefix)
	if err != nil {
		log.GetLogger().Errorf("failed to count reserved instance labels, err:%s", err.Error())
		return 0, err
	}
	return cnt, nil
}

// GetReserveInstanceConfigList get reserved instance config list
func GetReserveInstanceConfigList(ctx server.Context, clusterID string,
	info model.ReserveInsBaseInfo,
) ([]model.ReserveInstanceConfig, error) {
	keyPrefix := buildReserveInstanceWithLabelKey(clusterID, info.TenantID, info.FuncName, info.Version, "")
	decode := func(k, v string) (interface{}, error) {
		data := metadata.ReserveInstanceMetaData{}
		err := json.Unmarshal([]byte(v), &data)
		if err != nil {
			log.GetLogger().Errorf("failed to unmarshal instance config info, err: %s", err)
			return nil, errmsg.UnmarshalError
		}
		instanceCong := model.ReserveInstanceConfig{}
		instanceCong.InstanceConfig.MaxInstance = data.InstanceMetaData.MaxInstance
		instanceCong.InstanceConfig.MinInstance = data.InstanceMetaData.MinInstance
		instanceCong.InstanceConfig.ClusterID = clusterID
		instanceCong.InstanceLabel = getLabelFromKey(k)
		return instanceCong, nil
	}
	prepStmt := db.eng.PrepareStream(ctx.Context(), keyPrefix, decode, EngineSortBy)
	stream, err := prepStmt.Execute()
	if err != nil {
		log.GetLogger().Errorf("failed to execute stream [reserved instance], err: %s", err.Error())
		return nil, errmsg.EtcdInternalError
	}

	var res []model.ReserveInstanceConfig
	for {
		i, err := stream.Next()
		if err != nil {
			if err == io.EOF {
				break
			}
			log.GetLogger().Errorf("failed to get next from stream [reserved instance]: %s", err.Error())
			return nil, errmsg.EtcdInternalError
		}
		tuple, ok := i.(model.ReserveInstanceConfig)
		if ok {
			res = append(res, tuple)
		}
	}
	return res, nil
}
