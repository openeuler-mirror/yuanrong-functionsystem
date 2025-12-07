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
	"errors"
	"fmt"
	"regexp"
	"time"

	"meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/storage/publish"
	"meta_service/function_repo/utils"
)

// CheckFunctionVersion check whether the number of function versions exceeds the upper limit
func CheckFunctionVersion(ctx server.Context, functionName string) error {
	total, err := storage.GetFunctionVersionSizeByName(ctx, functionName)
	if err != nil {
		return snerror.NewWithFmtMsg(errmsg.FunctionVersionNotFound, "failed to get function versions size: %s",
			err.Error())
	}
	versionMax := config.RepoCfg.FunctionCfg.VersionMax
	if uint(total) >= versionMax+1 {
		return snerror.NewWithFmtMsg(errmsg.FunctionVersionOutOfLimit, "the number of existing function "+
			"versions is greater than the set value: %d", total)
	}
	return nil
}

func checkRepublish(txn *storage.Txn, funcVersion storage.FunctionVersionValue, req model.PublishRequest) error {
	if funcVersion.Function.Version != "" {
		_, err := storage.GetFunctionVersion(txn, funcVersion.Function.Name,
			funcVersion.Function.Version)
		if err != nil {
			if err == errmsg.KeyNotFoundError {
				return nil
			}
			return snerror.NewWithFmtMsg(errmsg.FunctionNotFound, "failed to get function version: %s", err.Error())
		}
	}
	return nil
}

func checkVersionNumberDuplicate(txn *storage.Txn, funcVersion storage.FunctionVersionValue,
	req model.PublishRequest,
) error {
	_, err := storage.GetFunctionVersion(txn, funcVersion.Function.Name, funcVersion.Function.Version)
	if err == errmsg.KeyNotFoundError {
		return nil
	}
	return snerror.NewWithFmtMsg(errmsg.RepeatedPublishmentError, "duplicate version number")
}

func validateVersionNumber(versionNum string) error {
	matched, err := regexp.MatchString(`^[A-Za-z0-9][A-Za-z0-9._-]{0,40}[A-Za-z0-9]$|^[A-Za-z0-9]$`, versionNum)
	if err != nil {
		return err
	}
	if !matched {
		return errors.New("versionNumber can only contain digits, letters, dots(.), hyphens(-), and underscores(_), " +
			"and must start and end with a digit or letter, with less than 42 characters")
	}
	return nil
}

func validateAndSetDefaultVersionNumber(versionNumber string) (string, error) {
	if versionNumber == "" {
		return fmt.Sprintf("v%s", time.Now().Format("20060102-150405")), nil
	} else if err := validateVersionNumber(versionNumber); err != nil {
		return "", err
	} else {
		return versionNumber, nil
	}
}

func checkPublishFunctionParams(ctx server.Context, funcName string, req model.PublishRequest, txn *storage.Txn) (
	storage.FunctionVersionValue, error,
) {
	defaultVersion := utils.GetDefaultVersion()
	if req.Kind == constants.Faas {
		defaultVersion = utils.GetDefaultFaaSVersion()
	}
	funcVersion, err := storage.GetFunctionVersion(txn, funcName, defaultVersion)
	if err != nil {
		return storage.FunctionVersionValue{}, snerror.NewWithFmtMsg(errmsg.FunctionNotFound,
			"failed to get function version")
	}
	if req.RevisionID != funcVersion.FunctionVersion.RevisionID {
		return storage.FunctionVersionValue{}, snerror.NewWithFmtMsg(errmsg.RevisionIDError,
			"revisionId is non latest version")
	}
	if err = checkRepublish(txn, funcVersion, req); err != nil {
		return storage.FunctionVersionValue{}, err
	}
	// update latest function version
	if funcVersion.Function.Version, err = validateAndSetDefaultVersionNumber(req.VersionNumber); err != nil {
		return storage.FunctionVersionValue{}, err
	}
	if err = checkVersionNumberDuplicate(txn, funcVersion, req); err != nil {
		return storage.FunctionVersionValue{}, err
	}
	if funcVersion.FunctionVersion.Package.StorageType == constants.S3StorageType &&
		funcVersion.FunctionVersion.Package.BucketID == "" && funcVersion.FunctionVersion.Package.ObjectID == "" {
		return storage.FunctionVersionValue{}, errors.New("empty function version's bucket info")
	}

	return funcVersion, nil
}

// PublishFunction publish function version meta data and register function version value to etcd
func PublishFunction(ctx server.Context, funcName string, req model.PublishRequest) (model.PublishResponse, error) {
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()
	funcVersion, err := checkPublishFunctionParams(ctx, funcName, req, txn)
	if err != nil {
		return model.PublishResponse{}, err
	}
	if err = storage.UpdateFunctionVersion(txn, funcVersion); err != nil {
		return model.PublishResponse{}, errors.New("failed to update function version")
	}

	// publish function version meta data
	publishFuncVersion := buildPublishFunctionVersion(req.VersionDesc, funcVersion)
	if err = storage.CreateFunctionVersion(txn, publishFuncVersion); err != nil {
		return model.PublishResponse{}, err
	}

	if err = storage.CreateFunctionStatus(txn, funcName, publishFuncVersion.FunctionVersion.Version); err != nil {
		return model.PublishResponse{}, err
	}

	// publish function version register data
	if err = publish.SavePublishFuncVersion(txn, publishFuncVersion); err != nil {
		return model.PublishResponse{}, err
	}
	if err = txn.Commit(); err != nil {
		return model.PublishResponse{}, err
	}

	// recollect the func version
	rsp, err := buildGetFunctionResponse(ctx, publishFuncVersion, "")
	if err != nil {
		log.GetLogger().Errorf("failed to build function response, %s", err)
		return model.PublishResponse{}, err
	}
	if rsp.LastModified == "" {
		rsp.LastModified = rsp.Created
	}
	return rsp, nil
}

func buildPublishFunctionVersion(desc string, funcVersion storage.FunctionVersionValue) storage.FunctionVersionValue {
	funcVersion.FunctionVersion.PublishTime = utils.NowTimeF()
	funcVersion.FunctionVersion.Description = desc
	funcVersion.FunctionVersion.Version = funcVersion.Function.Version
	return funcVersion
}
