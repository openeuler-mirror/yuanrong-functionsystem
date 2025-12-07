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

package service

import (
	"time"

	"meta_service/common/logger/log"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/storage/publish"
	"meta_service/function_repo/utils"
)

const (
	totalWeight = 100
)

// CheckFuncVersion check if alias's function version exist
func CheckFuncVersion(txn *storage.Txn, fName, fVersion string) error {
	isExist, err := storage.IsFuncVersionExist(txn, fName, fVersion)
	if err != nil {
		log.GetLogger().Errorf("failed to check function version, error: %s", err.Error())
		return err
	}
	if !isExist {
		return errmsg.New(errmsg.FunctionVersionNotFound, fName, fVersion)
	}
	return nil
}

// CheckAliasName check if alias's name exist
func CheckAliasName(txn *storage.Txn, fName, aliasName string) error {
	isExist, err := storage.IsAliasNameExist(txn, fName, aliasName)
	if err != nil {
		log.GetLogger().Errorf("failed to check alias name, error: %s", err.Error())
		return err
	}
	if isExist {
		return errmsg.New(errmsg.AliasNameAlreadyExists)
	}
	// aliasName doesn't exist, check successfully
	return nil
}

// CheckFunctionAliasNum check if total alias number whether is out of range
func CheckFunctionAliasNum(txn *storage.Txn, funcName string) error {
	number, err := storage.GetAliasNumByFunctionName(txn, funcName)
	if err != nil {
		log.GetLogger().Errorf("failed to get alias num by function name version, error: %s", err.Error())
		return err
	}
	if uint(number) >= config.RepoCfg.FunctionCfg.AliasMax {
		return errmsg.New(errmsg.AliasOutOfLimit, config.RepoCfg.FunctionCfg.AliasMax)
	}
	return nil
}

// CheckRevisionID check if revisionID is the same
func CheckRevisionID(txn *storage.Txn, fName, aName, revisionID string) (storage.AliasValue, error) {
	alias, err := storage.GetAlias(txn, fName, aName)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return alias, errmsg.New(errmsg.AliasNameNotFound, utils.RemoveServiceID(fName), aName)
		}
		log.GetLogger().Errorf("failed to get alias, error: %s", err.Error())
		return alias, err
	}

	if revisionID != alias.RevisionID {
		return storage.AliasValue{}, errmsg.New(errmsg.RevisionIDError)
	}
	return alias, nil
}

// CheckRoutingConfig check if routingConfig is illegal
func CheckRoutingConfig(txn *storage.Txn, fName string, routingConfig map[string]int) error {
	sum := 0
	for k, v := range routingConfig {
		if err := CheckFuncVersion(txn, fName, k); err != nil {
			return errmsg.New(errmsg.FunctionVersionNotFound, fName, k)
		}
		sum += v
	}
	if sum != totalWeight {
		return errmsg.New(errmsg.TotalRoutingWeightNotOneHundred)
	}
	return nil
}

func buildAlias(fName string, req model.AliasRequest) storage.AliasValue {
	revisionID := utils.GetUTCRevisionID()
	alias := storage.AliasValue{
		Name:            req.Name,
		FunctionName:    fName,
		FunctionVersion: req.FunctionVersion,
		RevisionID:      revisionID,
		Description:     req.Description,
		RoutingConfig:   req.RoutingConfig,
		CreateTime:      time.Now(),
		UpdateTime:      time.Now(),
	}
	return alias
}

func buildAliasResponse(ctx server.Context, alias storage.AliasValue) (model.AliasResponse, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return model.AliasResponse{}, err
	}
	aliasURN := utils.BuildAliasURN(t.BusinessID, t.TenantID, t.ProductID, alias.FunctionName, alias.Name)
	response := model.AliasResponse{
		AliasURN:        aliasURN,
		Name:            alias.Name,
		FunctionVersion: alias.FunctionVersion,
		Description:     alias.Description,
		RevisionID:      alias.RevisionID,
		RoutingConfig:   alias.RoutingConfig,
	}
	return response, nil
}

func buildAliasListResponse(ctx server.Context, aliases []storage.AliasValue) (model.AliasListQueryResponse, error) {
	resp := model.AliasListQueryResponse{}
	resp.Total = len(aliases)
	resp.Aliases = make([]model.AliasResponse, resp.Total)
	for i, v := range aliases {
		aliasResp, err := buildAliasResponse(ctx, v)
		if err != nil {
			return model.AliasListQueryResponse{}, err
		}
		resp.Aliases[i] = aliasResp
	}
	return resp, nil
}

func buildAliasEtcd(ctx server.Context, alias storage.AliasValue) (publish.AliasEtcd, error) {
	t, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return publish.AliasEtcd{}, err
	}

	aliasURN := utils.BuildAliasURN(t.BusinessID, t.TenantID, t.ProductID, alias.FunctionName, alias.Name)
	funcURN := utils.BuildFunctionURN(t.BusinessID, t.TenantID, t.ProductID, alias.FunctionName)
	funcVerURN := utils.BuildFunctionVersionURN(
		t.BusinessID, t.TenantID, t.ProductID, alias.FunctionName, alias.FunctionVersion)

	aliasEtcd := publish.AliasEtcd{
		AliasURN:           aliasURN,
		FunctionURN:        funcURN,
		FunctionVersionURN: funcVerURN,
		Name:               alias.Name,
		FunctionVersion:    alias.FunctionVersion,
		RevisionID:         alias.RevisionID,
		Description:        alias.Description,
	}
	aliasEtcd.RoutingConfig = make([]publish.AliasRoutingEtcd, len(alias.RoutingConfig))
	i := 0
	for k, v := range alias.RoutingConfig {
		funcVerURN = utils.BuildFunctionVersionURN(t.BusinessID, t.TenantID, t.ProductID, alias.FunctionName, k)
		aliasEtcd.RoutingConfig[i].FunctionVersionURN = funcVerURN
		aliasEtcd.RoutingConfig[i].Weight = v
		i++
	}

	return aliasEtcd, nil
}

func mergeAlias(req model.AliasUpdateRequest, alias storage.AliasValue) storage.AliasValue {
	alias.FunctionVersion = req.FunctionVersion
	if req.Description != "" {
		alias.Description = req.Description
	}
	if len(req.RoutingConfig) != 0 {
		alias.RoutingConfig = req.RoutingConfig
	}
	alias.UpdateTime = time.Now()
	return alias
}

// CreateAlias create alias implement
func CreateAlias(ctx server.Context, fName string, req model.AliasRequest) (
	resp model.AliasCreateResponse, err error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	if err = CheckFuncVersion(txn, fName, req.FunctionVersion); err != nil {
		log.GetLogger().Errorf("failed to check function version, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}
	if err = CheckAliasName(txn, fName, req.Name); err != nil {
		log.GetLogger().Errorf("failed to check alias name, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}
	if err = CheckFunctionAliasNum(txn, fName); err != nil {
		log.GetLogger().Errorf("failed to check function alias number, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}
	if err = CheckRoutingConfig(txn, fName, req.RoutingConfig); err != nil {
		log.GetLogger().Errorf("failed to check routing configuration, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	alias := buildAlias(fName, req)
	err = storage.CreateAlias(txn, alias)
	if err != nil {
		log.GetLogger().Errorf("failed to save alias, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	aliasEtcd, err := buildAliasEtcd(ctx, alias)
	if err != nil {
		log.GetLogger().Errorf("failed to build alias for etcd watch, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	err = publish.CreateAliasEtcd(txn, fName, aliasEtcd)
	if err != nil {
		log.GetLogger().Errorf("failed to save aliasEtcd, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	// commit transaction to storage
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit create alias transaction, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	aliasRes, err := buildAliasResponse(ctx, alias)
	if err != nil {
		log.GetLogger().Errorf("failed to build alias response, error: %s", err.Error())
		return model.AliasCreateResponse{}, err
	}

	response := model.AliasCreateResponse{
		AliasResponse: aliasRes,
	}
	return response, nil
}

// UpdateAlias update alias implement
func UpdateAlias(ctx server.Context, fName, aName string, req model.AliasUpdateRequest) (
	resp model.AliasUpdateResponse, err error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	alias, err := CheckRevisionID(txn, fName, aName, req.RevisionID)
	if err != nil {
		log.GetLogger().Errorf("failed to check alias, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}
	if err = CheckFuncVersion(txn, fName, req.FunctionVersion); err != nil {
		log.GetLogger().Errorf("failed to check function version, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}
	if req.RoutingConfig != nil && len(req.RoutingConfig) != 0 {
		if err = CheckRoutingConfig(txn, fName, req.RoutingConfig); err != nil {
			log.GetLogger().Errorf("failed to check routing configuration, error: %s", err.Error())
			return model.AliasUpdateResponse{}, err
		}
	}

	updateAlias := mergeAlias(req, alias)
	err = storage.CreateAlias(txn, updateAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to save alias, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}

	aliasEtcd, err := buildAliasEtcd(ctx, updateAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to build alias for etcd watch, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}
	err = publish.CreateAliasEtcd(txn, fName, aliasEtcd)
	if err != nil {
		log.GetLogger().Errorf("failed to save aliasEtcd, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}

	// commit transaction to storage
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit update alias transaction, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}

	aliasRes, err := buildAliasResponse(ctx, updateAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to build alias response, error: %s", err.Error())
		return model.AliasUpdateResponse{}, err
	}
	updateRes := model.AliasUpdateResponse{
		AliasResponse: aliasRes,
	}

	return updateRes, nil
}

// DeleteAlias delete alias implement
func DeleteAlias(ctx server.Context, req model.AliasDeleteRequest) (err error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	alias, err := storage.GetAlias(txn, req.FunctionName, req.AliasName)
	if err != nil {
		log.GetLogger().Errorf(" failed to get alias, error: %s", err.Error())
		return err
	}

	var routingVers []string
	for k := range alias.RoutingConfig {
		routingVers = append(routingVers, k)
	}

	if req.AliasName != "" {
		err = storage.DeleteAlias(txn, req.FunctionName, req.AliasName, routingVers)
	} else {
		err = storage.DeleteAliasByFunctionName(txn, req.FunctionName)
	}
	if err != nil {
		return err
	}

	// commit transaction to storage
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit delete alias transaction, error: %s", err.Error())
		return err
	}

	return nil
}

// GetAlias get single alias information
func GetAlias(ctx server.Context, req model.AliasQueryRequest) (resp model.AliasQueryResponse, err error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	alias, err := storage.GetAlias(txn, req.FunctionName, req.AliasName)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return model.AliasQueryResponse{}, errmsg.New(
				errmsg.AliasNameNotFound, utils.RemoveServiceID(req.FunctionName), req.AliasName)
		}
		log.GetLogger().Errorf("failed to get alias, error: %s", err.Error())
		return model.AliasQueryResponse{}, err
	}

	// commit transaction to storage
	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit get alias transaction, error: %s", err.Error())
		return model.AliasQueryResponse{}, err
	}

	aliasRes, err := buildAliasResponse(ctx, alias)
	if err != nil {
		log.GetLogger().Errorf("failed to build alias response, error: %s", err.Error())
		return model.AliasQueryResponse{}, err
	}
	getAliasRes := model.AliasQueryResponse{
		AliasResponse: aliasRes,
	}

	return getAliasRes, nil
}

// GetAliaseList get aliases list
func GetAliaseList(ctx server.Context, req model.AliasListQueryRequest) (
	resp model.AliasListQueryResponse, err error) {
	aliases, err := storage.GetAliasesByPage(ctx, req.FunctionName, req.PageIndex, req.PageSize)
	if err != nil {
		log.GetLogger().Errorf("failed to get alias list, error: %s", err.Error())
		return model.AliasListQueryResponse{}, err
	}

	aliasesResp, err := buildAliasListResponse(ctx, aliases)
	if err != nil {
		log.GetLogger().Errorf("failed to build aliases response, error: %s", err.Error())
		return model.AliasListQueryResponse{}, err
	}

	return aliasesResp, nil
}
