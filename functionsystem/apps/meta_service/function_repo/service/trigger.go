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
	"meta_service/common/snerror"
	"meta_service/common/uuid"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/storage/publish"
	"meta_service/function_repo/utils"
	"meta_service/function_repo/utils/constants"
)

const (
	// triggerMaxCount is the maximum of trigger
	triggerMaxCount = 32
	appSecretMaxLen = 256
)

var triggerSpecTable = map[string]struct {
	Create func(txn *storage.Txn, raw interface{}, funcInfo model.FunctionQueryInfo, funcID, triggerID string,
	) (storage.TriggerSpec, error)

	Update func(raw interface{}) (storage.TriggerSpec, string, error)
}{
	model.HTTPType: {
		Create: createHTTPTriggerSpec,
	},
}

func checkFunctionNameAndVersion(txn *storage.Txn, funcName string, version string) error {
	_, err := storage.GetFunctionVersion(txn, funcName, version)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("this version [%s] of function [%s] does not exist", funcName, version)
			return errmsg.New(errmsg.FunctionVersionNotFound, funcName, version)
		}
		log.GetLogger().Errorf("failed to get function version, error: %s", err.Error())
		return err
	}
	return nil
}

func checkFuncInfo(txn *storage.Txn, info model.FunctionQueryInfo) error {
	if err := checkFunctionNameAndVersion(txn, info.FunctionName, constants.DefaultVersion); err != nil {
		log.GetLogger().Errorf("failed to check function name and version, error: %s", err.Error())
		return err
	}
	if info.AliasName != "" {
		exist, err := storage.AliasNameExist(txn, info.FunctionName, info.AliasName)
		if err != nil {
			log.GetLogger().Errorf("failed to get aliasname, error: %s", err.Error())
			return err
		}
		if !exist {
			log.GetLogger().Errorf(
				"this alias name [%s] of function [%s] does not exist", info.FunctionName, info.AliasName)
			return errmsg.New(errmsg.AliasNameNotFound, utils.RemoveServiceID(info.FunctionName), info.AliasName)
		}
		return nil
	}
	if err := checkFunctionNameAndVersion(txn, info.FunctionName, info.FunctionVersion); err != nil {
		log.GetLogger().Errorf("failed to check function name and version, error: %s", err.Error())
		return err
	}
	return nil
}

func getVerOrAlias(i model.FunctionQueryInfo) string {
	var verOrAlias string
	if i.AliasName != "" {
		verOrAlias = i.AliasName
	} else {
		verOrAlias = i.FunctionVersion
	}
	return verOrAlias
}

func createTrigger(txn *storage.Txn, req model.TriggerCreateRequest, funcInfo model.FunctionQueryInfo,
) (storage.TriggerValue, error) {
	info := storage.TriggerValue{}
	info.TriggerID = uuid.New().String()
	info.FuncName = funcInfo.FunctionName
	info.TriggerType = req.TriggerType
	info.RevisionID = utils.GetUTCRevisionID()
	info.CreateTime = time.Now()
	info.UpdateTime = time.Now()

	var err error
	if t, ok := triggerSpecTable[req.TriggerType]; ok {
		info.EtcdSpec, err = t.Create(txn, req.Spec, funcInfo, req.FuncID, info.TriggerID)
	} else {
		err = errmsg.NewParamError("invalid triggerType: %s", info.TriggerType)
	}
	if err != nil {
		log.GetLogger().Errorf("failed to convert %s trigger spec, error: %s", info.TriggerType, err.Error())
		return storage.TriggerValue{}, err
	}
	return info, nil
}

func buildModelTriggerSpec(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
	triggerSpec := model.TriggerSpec{
		FuncID:      funcID,
		TriggerID:   info.TriggerID,
		TriggerType: info.TriggerType,
	}
	spec, err = info.EtcdSpec.BuildModelSpec(triggerSpec)
	if err != nil {
		log.GetLogger().Errorf("failed to convert spec, error: %s", err.Error())
	}
	return
}

func buildTriggerResponse(ctx server.Context, triggerInfo storage.TriggerValue, funcName, verOrAlias string,
) (model.TriggerInfo, error) {
	resp := model.TriggerInfo{}
	tenantInfo, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("invalid tenantInfo, error: %s", err.Error())
		return model.TriggerInfo{}, err
	}
	funcID := utils.BuildTriggerURN(tenantInfo, funcName, verOrAlias)
	resp.FuncID = funcID
	resp.TriggerType = triggerInfo.TriggerType
	resp.TriggerID = triggerInfo.TriggerID
	resp.RevisionID = triggerInfo.RevisionID
	resp.CreateTime = triggerInfo.CreateTime
	resp.UpdateTime = triggerInfo.UpdateTime
	resp.Spec, err = buildModelTriggerSpec(triggerInfo, funcID)
	if err != nil {
		log.GetLogger().Errorf("failed to convert spec, error: %s", err.Error())
		return model.TriggerInfo{}, err
	}
	return resp, nil
}

func updateTrigger(req model.TriggerUpdateRequest) (storage.TriggerFunctionIndexValue, storage.TriggerValue, error) {
	info := storage.TriggerValue{}
	info.TriggerID = req.TriggerID
	info.TriggerType = req.TriggerType
	info.RevisionID = req.RevisionID

	var (
		err    error
		funcID string
	)
	if t, ok := triggerSpecTable[req.TriggerType]; ok && t.Update != nil {
		info.EtcdSpec, funcID, err = t.Update(req.Spec)
	} else if t.Update == nil {
		err = errmsg.NewParamError("update function not found, triggerType: %s", info.TriggerType)
	} else {
		err = errmsg.NewParamError("invalid triggerType: %s", info.TriggerType)
	}
	if err != nil {
		log.GetLogger().Errorf("failed to convert %s trigger spec, error: %s", info.TriggerType, err.Error())
		return storage.TriggerFunctionIndexValue{}, storage.TriggerValue{}, err
	}
	funcInfo, err := CheckAndGetVerOrAlias(funcID, "")
	if err != nil {
		log.GetLogger().Errorf("failed to check version or alias, error: %s", err.Error())
		return storage.TriggerFunctionIndexValue{}, storage.TriggerValue{}, err
	}
	info.FuncName = funcInfo.FunctionName
	tfInfo := storage.TriggerFunctionIndexValue{}
	tfInfo.FunctionName = funcInfo.FunctionName
	tfInfo.VersionOrAlias = getVerOrAlias(funcInfo)
	return tfInfo, info, nil
}

func storeCreateTrigger(
	txn *storage.Txn, triggerInfo storage.TriggerValue, funcInfo model.FunctionQueryInfo, funcID string,
) error {
	spec, err := buildModelTriggerSpec(triggerInfo, funcID)
	if err != nil {
		log.GetLogger().Errorf("failed to convert spec, error: %s", err.Error())
		return err
	}

	verOrAlias := getVerOrAlias(funcInfo)
	if verOrAlias == "" {
		log.GetLogger().Errorf("empty version and alias")
		return snerror.NewWithFmtMsg(errmsg.EmptyAliasAndVersion, "empty version and alias")
	}

	err = storage.SaveTriggerInfo(txn, funcInfo.FunctionName, verOrAlias, triggerInfo.TriggerID, triggerInfo)
	if err != nil {
		log.GetLogger().Errorf("failed to save trigger, error: %s", err.Error())
		return err
	}
	if err := publish.AddTrigger(txn, funcInfo.FunctionName, verOrAlias, triggerInfo, spec); err != nil {
		log.GetLogger().Errorf("failed to publish trigger, error: %s", err.Error())
		return err
	}

	return nil
}

func storeUpdateTrigger(
	txn *storage.Txn, info storage.TriggerValue, funcInfo storage.TriggerFunctionIndexValue, funcID string,
) error {
	triggerInfo, err := storage.GetTriggerInfo(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, info.TriggerID)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("this triggerid [%s] does not exist", info.TriggerID)
			return errmsg.New(errmsg.TriggerIDNotFound, info.TriggerID)
		}
		log.GetLogger().Errorf("failed to get trigger, error: %s", err.Error())
		return err
	}

	if triggerInfo.RevisionID != info.RevisionID {
		log.GetLogger().Errorf("revisionId is not the same as latest versions")
		return snerror.NewWithFmtMsg(errmsg.RevisionIDError, "different revisionId")
	}
	info.RevisionID = utils.GetUTCRevisionID()
	info.FuncName = triggerInfo.FuncName
	info.CreateTime = triggerInfo.CreateTime
	info.UpdateTime = time.Now()
	spec, err := buildModelTriggerSpec(info, funcID)
	if err != nil {
		log.GetLogger().Errorf("failed to convert request, error: %s", err.Error())
		return err
	}

	if funcInfo.VersionOrAlias == "" {
		log.GetLogger().Errorf("empty version and alias")
		return snerror.NewWithFmtMsg(errmsg.EmptyAliasAndVersion, "empty version and alias")
	}

	err = storage.SaveTriggerInfo(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, info.TriggerID, info)
	if err != nil {
		log.GetLogger().Errorf("failed to save trigger, error: %s", err.Error())
		return err
	}
	if err := publish.AddTrigger(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, info, spec); err != nil {
		log.GetLogger().Errorf("failed to publish trigger, error: %s", err.Error())
		return err
	}
	return nil
}

// CreateTrigger creates trigger info
func CreateTrigger(ctx server.Context, req model.TriggerCreateRequest) (model.TriggerResponse, error) {
	resp := model.TriggerResponse{}
	funcID := req.FuncID
	funcInfo, err := ParseFunctionInfo(ctx, funcID, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function info, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	if err = checkFuncInfo(txn, funcInfo); err != nil {
		log.GetLogger().Errorf("failed to check function info, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	verOrAlias := getVerOrAlias(funcInfo)
	triggerList, err := storage.GetTriggerByFunctionNameVersion(txn, funcInfo.FunctionName, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	if len(triggerList) >= triggerMaxCount {
		log.GetLogger().Errorf("trigger count reaches the maximum value")
		return model.TriggerResponse{}, errmsg.New(errmsg.TriggerNumOutOfLimit, triggerMaxCount)
	}

	triggerInfo, err := createTrigger(txn, req, funcInfo)
	if err != nil {
		log.GetLogger().Errorf("failed to change request to trigger info, error: %s", err.Error())
		e, ok := err.(snerror.SNError)
		if !ok {
			e = errmsg.NewParamError("invalid spec in request")
		}
		return model.TriggerResponse{}, e
	}

	if err := storeCreateTrigger(txn, triggerInfo, funcInfo, funcID); err != nil {
		return model.TriggerResponse{}, err
	}

	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	resp.TriggerInfo, err = buildTriggerResponse(ctx, triggerInfo, funcInfo.FunctionName, verOrAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to convert trigger info to response, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	return resp, nil
}

// GetTrigger gets trigger info by trigger id
func GetTrigger(ctx server.Context, tid string) (model.TriggerResponse, error) {
	var triggerInfo storage.TriggerValue
	resp := model.TriggerResponse{}

	funcInfo, err := storage.GetFunctionInfoByTriggerID(ctx, tid)
	if err != nil {
		log.GetLogger().Errorf("failed to get function info by triggerid from storage, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	triggerInfo, err = storage.GetTriggerInfoByTriggerID(ctx, funcInfo.FunctionName, funcInfo.VersionOrAlias, tid)
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger info by triggerid from storage, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	resp.TriggerInfo, err = buildTriggerResponse(ctx, triggerInfo, funcInfo.FunctionName, funcInfo.VersionOrAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to convert trigger info to response, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	return resp, nil
}

// GetTriggerList get trigger infos by function id
func GetTriggerList(ctx server.Context, pageIndex, pageSize int, fid string) (model.TriggerListGetResponse, error) {
	resp := model.TriggerListGetResponse{}
	funcInfo, err := ParseFunctionInfo(ctx, fid, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function info, error: %s", err.Error())
		return model.TriggerListGetResponse{}, err
	}

	verOrAlias := getVerOrAlias(funcInfo)
	triggerList, err := storage.GetTriggerInfoList(ctx, funcInfo.FunctionName, verOrAlias, pageIndex, pageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			resp.Count = 0
			return resp, nil
		}
		log.GetLogger().Errorf("failed to get trigger list from storage, error: %s", err.Error())
		return model.TriggerListGetResponse{}, err
	}

	resp.Count = len(triggerList)
	resp.TriggerList = make([]model.TriggerInfo, resp.Count, resp.Count)
	for index, triggerInfo := range triggerList {
		respTrigger, err := buildTriggerResponse(ctx, triggerInfo, funcInfo.FunctionName, verOrAlias)
		if err != nil {
			log.GetLogger().Errorf("failed to convert triggerInfo to response, error: %s", err.Error())
			return model.TriggerListGetResponse{}, err
		}
		resp.TriggerList[index] = respTrigger
	}
	return resp, nil
}

// DeleteTriggerByID deletes trigger info by trigger id
func DeleteTriggerByID(ctx server.Context, tid string) error {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	funcInfo, err := storage.GetFunctionInfo(txn, tid)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("this triggerid [%s] does not exist", tid)
			return errmsg.New(errmsg.FunctionNotFound, tid)
		}
		log.GetLogger().Errorf("failed to get function, error: %s", err.Error())
		return err
	}

	triggerInfo, err := storage.GetTriggerInfo(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, tid)
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger, error: %s", err.Error())
		return err
	}
	if err := storage.DeleteTrigger(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, tid); err != nil {
		log.GetLogger().Errorf("failed to delete trigger, error: %s", err.Error())
		return err
	}
	if err := publish.DeleteTrigger(txn, funcInfo.FunctionName, funcInfo.VersionOrAlias, triggerInfo); err != nil {
		log.GetLogger().Errorf("failed to delete publishings, error: %s", err.Error())
		return err
	}

	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteTriggerByFuncID deletes trigger info by function id
func DeleteTriggerByFuncID(ctx server.Context, fid string) error {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	funcInfo, err := ParseFunctionInfo(ctx, fid, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get function info, error: %s", err.Error())
		return err
	}

	verOrAlias := getVerOrAlias(funcInfo)
	if err := DeleteTriggerByFuncNameVersion(txn, funcInfo.FunctionName, verOrAlias); err != nil {
		log.GetLogger().Errorf("failed to delete publishings, error: %s", err.Error())
		return err
	}
	if err := txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit, error: %s", err.Error())
		return err
	}
	return nil
}

// DeleteTriggerByFuncName deletes trigger all info by function name
func DeleteTriggerByFuncName(txn storage.Transaction, funcName string) error {
	if err := publish.DeleteTriggerByFuncName(txn, funcName); err != nil {
		log.GetLogger().Errorf("failed to delete publishings, error: %s", err.Error())
		return err
	}

	if err := storage.DeleteTriggerByFunctionNameVersion(txn, funcName, ""); err != nil {
		log.GetLogger().Errorf("failed to delete trigger, error: %s", err)
		return err
	}

	return nil
}

// DeleteTriggerByFuncNameVersion deletes trigger publish info by function name and version
func DeleteTriggerByFuncNameVersion(txn storage.Transaction, funcName string, versionOrAlias string) error {
	infos, err := storage.GetTriggerByFunctionNameVersion(txn, funcName, versionOrAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to get trigger, error %s", err.Error())
		return err
	}

	err = storage.DeleteTriggerByFunctionNameVersion(txn, funcName, versionOrAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to delete trigger by function %s version %s, error: %s",
			funcName, versionOrAlias, err.Error())
		return err
	}

	for _, triggerInfo := range infos {
		if err := publish.DeleteTrigger(txn, funcName, versionOrAlias, triggerInfo); err != nil {
			log.GetLogger().Errorf("failed to delete publishings, error: %s", err.Error())
			return err
		}
	}
	return nil
}

// UpdateTrigger update trigger info
func UpdateTrigger(ctx server.Context, req model.TriggerUpdateRequest) (model.TriggerResponse, error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	resp := model.TriggerResponse{}
	funcInfo, info, err := updateTrigger(req)
	if err != nil {
		log.GetLogger().Errorf("failed to convert request to trigger info, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}
	tenantInfo, err := ctx.TenantInfo()
	if err != nil {
		log.GetLogger().Errorf("failed to get tenant info, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	funcID := utils.BuildTriggerURN(tenantInfo, funcInfo.FunctionName, funcInfo.VersionOrAlias)
	if err := storeUpdateTrigger(txn, info, funcInfo, funcID); err != nil {
		log.GetLogger().Errorf("failed to convert request, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	err = txn.Commit()
	if err != nil {
		log.GetLogger().Errorf("failed to commit, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}

	resp.TriggerInfo, err = buildTriggerResponse(ctx, info, funcInfo.FunctionName, funcInfo.VersionOrAlias)
	if err != nil {
		log.GetLogger().Errorf("failed to convert triggerInfo to response, error: %s", err.Error())
		return model.TriggerResponse{}, err
	}
	return resp, nil
}
