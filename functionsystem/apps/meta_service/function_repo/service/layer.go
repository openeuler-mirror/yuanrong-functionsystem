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
	"bytes"
	"encoding/base64"
	"encoding/json"
	"strconv"
	"strings"
	"time"

	common "meta_service/common/constants"
	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/common/timeutil"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/pkgstore"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/utils"
)

const (
	maxLayerSize             = 1000
	maxLayerVersionSize      = 10000
	maxLayerVersionNum       = 1000000
	compatibleRuntimeMinSize = 1
	compatibleRuntimeMaxSize = 5
	defaultBase              = 10
)

func buildLayer(
	ctx server.Context, mlayer *model.Layer, layerName string, layerVersion int, layer storage.LayerValue,
) error {
	mlayer.Name = layerName
	mlayer.Version = layerVersion

	info, err := ctx.TenantInfo()
	if err != nil {
		return err
	}
	mlayer.LayerURN = utils.BuildLayerURN(info.BusinessID, info.TenantID, info.ProductID, layerName)
	mlayer.LayerVersionURN = utils.BuildLayerVersionURN(
		info.BusinessID, info.TenantID, info.ProductID, layerName, strconv.Itoa(layerVersion))

	mlayer.LayerSize = layer.Package.Size
	mlayer.LayerSHA256 = layer.Package.Signature
	mlayer.CreateTime = layer.CreateTime
	mlayer.UpdateTime = layer.UpdateTime
	mlayer.CompatibleRuntimes = layer.CompatibleRuntimes
	mlayer.Description = layer.Description
	mlayer.LicenseInfo = layer.LicenseInfo
	return nil
}

func isCompatibleRuntimeValid(cps []string) error {
	seen := make(map[string]interface{}, len(cps))

	for _, cp := range cps {
		var found bool
		for _, vcp := range config.RepoCfg.CompatibleRuntimeType {
			if cp == vcp {
				found = true
				break
			}
		}
		if !found {
			log.GetLogger().Errorf("compatible runtime %v is invalid", cps)
			return errmsg.New(errmsg.CompatibleRuntimeError)
		}

		if _, ok := seen[cp]; ok {
			log.GetLogger().Errorf("duplicate compatible runtimes %v", cps)
			return errmsg.New(errmsg.DuplicateCompatibleRuntimes)
		}
		seen[cp] = struct{}{}
	}
	return nil
}

// ParseLayerInfo parses input layer URN or layer name.
func ParseLayerInfo(ctx server.Context, s string) (model.LayerQueryInfo, error) {
	info, valid := utils.ParseLayerVerInfoFromURN(s)
	if !valid {
		// s is just the layerName
		return model.LayerQueryInfo{LayerName: s}, nil
	}
	tenant, err := ctx.TenantInfo()
	if err != nil {
		return model.LayerQueryInfo{}, err
	}
	if info.BusinessID != tenant.BusinessID || info.TenantInfo != tenant.TenantID {
		log.GetLogger().Errorf(
			"query tenant %s, %s is not the same as header tenant %s, %s",
			info.BusinessID, info.TenantInfo, tenant.BusinessID, tenant.TenantID)
		return model.LayerQueryInfo{}, snerror.New(errmsg.InvalidFunctionLayer, "urn "+s+" is invalid")
	}
	return model.LayerQueryInfo{LayerName: info.LayerName, LayerVersion: info.LayerVersion}, nil
}

func getLayerNextVersion(txn *storage.Txn, layerName string) (int, error) {
	size, version, err := storage.GetLayerSizeAndLatestVersion(txn, layerName)
	if err != nil {
		if err != errmsg.KeyNotFoundError {
			log.GetLogger().Errorf("failed to get layer latest version: %s", err.Error())
			return 0, err
		}
		n, err := storage.CountLayerTx(txn)
		if err != nil {
			log.GetLogger().Errorf("failed to count layer: %s", err.Error())
			return 0, err
		}
		if n >= maxLayerSize {
			log.GetLogger().Errorf("max layer size has reached")
			return 0, errmsg.New(errmsg.TenantLayerSizeOutOfLimit, n, maxLayerSize)
		}
		return storage.NewLayerVersion, nil
	}
	if size > maxLayerVersionSize {
		log.GetLogger().Errorf("max layer version size has reached")
		return 0, errmsg.New(errmsg.LayerVersionSizeOutOfLimit, size, maxLayerVersionSize)
	}
	if version >= maxLayerVersionNum {
		log.GetLogger().Errorf("max layer version num has reached")
		return 0, errmsg.New(errmsg.LayerVersionNumOutOfLimit, maxLayerVersionNum)
	}
	version++
	return version, nil
}

func createPackage(ctx server.Context, layerName, base64Content string) (pkgstore.Package, error) {
	name := layerName + "-" + strconv.FormatInt(timeutil.NowUnixMillisecond(), defaultBase)
	b, err := base64.StdEncoding.DecodeString(base64Content)
	if err != nil {
		log.GetLogger().Errorf("failed to decode req.ZipFile, %s", err.Error())
		return nil, snerror.New(errmsg.SaveFileError, "failed to decode req.ZipFile")
	}
	pkg, err := pkgstore.NewPackage(ctx, name, bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return nil, err
	}
	return pkg, nil
}

// CreateLayer handles the request of creating a layer
func CreateLayer(ctx server.Context, layerName string, req model.CreateLayerRequest) (model.Layer, error) {
	compatibleRuntimes, err := getCompatibleRuntimes(req.CompatibleRuntimes)
	if err != nil {
		return model.Layer{}, err
	}
	if err := isCompatibleRuntimeValid(compatibleRuntimes); err != nil {
		return model.Layer{}, err
	}

	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	version, err := getLayerNextVersion(txn, layerName)
	if err != nil {
		return model.Layer{}, err
	}

	tempFile, err := getUploadFile(ctx)
	if err != nil {
		log.GetLogger().Errorf("failed to get upload file, err: %s", err.Error())
		return model.Layer{}, err
	}
	name := layerName + "-" + strconv.FormatInt(timeutil.NowUnixMillisecond(), 10)
	pkg, err := pkgstore.NewPackage(ctx, name, tempFile, req.ZipFileSize)
	if err != nil {
		return model.Layer{}, err
	}
	defer pkg.Close()

	uploader, err := pkgstore.NewUploader(ctx, pkg)
	if err != nil {
		return model.Layer{}, err
	}

	if err := storage.AddUncontrolled(ctx, uploader.BucketID(), uploader.ObjectID()); err != nil {
		log.GetLogger().Errorf("failed to add uncontrolled: %s", err.Error())
		return model.Layer{}, err
	}

	if err := uploader.Upload(); err != nil {
		storage.RemoveUncontrolled(ctx, uploader.BucketID(), uploader.ObjectID())
		return model.Layer{}, err
	}

	layer := createLayerValue(pkg, uploader, req, compatibleRuntimes)
	res, err := createLayerHelper(txn, layerName, version, layer)
	if err != nil {
		// remove uncontrolled iff pkg deletion is successful
		if e := uploader.Rollback(); e == nil {
			storage.RemoveUncontrolled(ctx, uploader.BucketID(), uploader.ObjectID())
		}
		return model.Layer{}, err
	}
	storage.RemoveUncontrolled(ctx, uploader.BucketID(), uploader.ObjectID())
	return res, err
}

func getCompatibleRuntimes(compatibleRuntimes string) ([]string, error) {
	var result []string
	err := json.Unmarshal([]byte(compatibleRuntimes), &result)
	if err != nil {
		return nil, err
	}
	if len(result) > compatibleRuntimeMaxSize || len(result) < compatibleRuntimeMinSize {
		return nil, errmsg.NewParamError(
			"compatibleRuntimes size must be between %d and %d", compatibleRuntimeMinSize, compatibleRuntimeMaxSize)
	}
	return result, nil
}

func createLayerValue(pkg pkgstore.Package, uploader pkgstore.Uploader,
	req model.CreateLayerRequest, compatibleRuntimes []string) storage.LayerValue {
	return storage.LayerValue{
		Package: storage.Package{
			StorageType: common.S3StorageType,
			S3Package: storage.S3Package{
				Signature: pkg.Signature(),
				Size:      pkg.Size(),
				BucketID:  uploader.BucketID(),
				ObjectID:  uploader.ObjectID(),
			},
		},
		CompatibleRuntimes: compatibleRuntimes,
		Description:        req.Description,
		LicenseInfo:        req.LicenseInfo,
		CreateTime:         time.Now(),
		UpdateTime:         time.Now(),
	}
}

func createLayerHelper(
	txn *storage.Txn, layerName string, layerVersion int, layer storage.LayerValue,
) (model.Layer, error) {
	if err := storage.CreateLayer(txn, layerName, layerVersion, layer); err != nil {
		log.GetLogger().Errorf("failed to create layer: %s", err.Error())
		return model.Layer{}, err
	}

	if err := txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit transaction: %s", err.Error())
		return model.Layer{}, err
	}

	var res model.Layer
	if err := buildLayer(txn.GetCtx(), &res, layerName, layerVersion, layer); err != nil {
		log.GetLogger().Errorf("failed to build layer")
		return model.Layer{}, err
	}
	return res, nil
}

// DeleteLayer handles the request of deleting all layer versions
func DeleteLayer(ctx server.Context, layerName string) error {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	layers, err := storage.GetLayerTx(txn, layerName)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer: %s", err.Error())
		return err
	}
	usedVersion, err := storage.IsLayerUsed(txn, layerName)
	if err != nil {
		log.GetLogger().Errorf("failed to check is layer used: %s", err.Error())
		return err
	}
	if usedVersion != 0 {
		log.GetLogger().Errorf("layer is still in used")
		return errmsg.New(errmsg.LayerIsUsed, usedVersion)
	}

	return deleteLayerHelper(txn, layers, layerName)
}

func deleteLayerHelper(txn *storage.Txn, layers []storage.LayerTuple, layerName string) error {
	for _, v := range layers {
		if err := storage.AddUncontrolledTx(txn, v.Value.Package.BucketID, v.Value.Package.ObjectID); err != nil {
			log.GetLogger().Errorf("failed to add uncontrolled: %s", err.Error())
			return err
		}
	}
	if err := storage.DeleteLayer(txn, layerName); err != nil {
		log.GetLogger().Errorf("failed to delete layer: %s", err.Error())
		return err
	}
	if err := txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit: %s", err.Error())
		return err
	}
	for _, v := range layers {
		if err := pkgstore.Delete(
			v.Value.Package.BucketID, v.Value.Package.ObjectID); err == nil {
			storage.RemoveUncontrolled(txn.GetCtx(), v.Value.Package.BucketID, v.Value.Package.ObjectID)
		}
	}
	return nil
}

// DeleteLayerVersion handles the request of deleting a specific layer version
func DeleteLayerVersion(ctx server.Context, layerName string, layerVersion int) error {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	layer, err := storage.GetLayerVersionTx(txn, layerName, layerVersion)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return snerror.NewWithFmtMsg(
				errmsg.LayerVersionNotFound,
				"this version [%d] of layer [%s] does not exist", layerVersion, layerName)
		}
		log.GetLogger().Errorf("failed to get layer version: %s", err.Error())
		return err
	}
	used, err := storage.IsLayerVersionUsed(txn, layerName, layerVersion)
	if err != nil {
		log.GetLogger().Errorf("failed to check is layer used failed: %s", err.Error())
		return err
	}
	if used {
		log.GetLogger().Errorf("layer is still in used")
		return errmsg.New(errmsg.LayerIsUsed, layerVersion)
	}

	if err := storage.AddUncontrolledTx(txn, layer.Package.BucketID, layer.Package.ObjectID); err != nil {
		log.GetLogger().Errorf("failed to add uncontrolled: %s", err.Error())
		return err
	}
	if err := storage.DeleteLayerVersion(txn, layerName, layerVersion); err != nil {
		log.GetLogger().Errorf("failed to delete layer: %s", err.Error())
		return err
	}
	if err := txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit: %s", err.Error())
		return err
	}
	if err := pkgstore.Delete(layer.Package.BucketID, layer.Package.ObjectID); err == nil {
		storage.RemoveUncontrolled(ctx, layer.Package.BucketID, layer.Package.ObjectID)
	}
	return nil
}

func filterStream(stream *storage.LayerPrepareStmt, compatibleRuntime string) *storage.LayerPrepareStmt {
	stream = stream.Filter(func(key storage.LayerKey, val storage.LayerValue) bool {
		var compatible bool
		for _, cr := range val.CompatibleRuntimes {
			if cr == compatibleRuntime {
				compatible = true
				break
			}
		}
		return compatible
	})
	return stream
}

func getLayerListHelper(
	ctx server.Context, stream *storage.LayerPrepareStmt, compatibleRuntime string, pageIndex, pageSize int,
) (model.LayerList, error) {
	if compatibleRuntime != "" {
		stream = filterStream(stream, compatibleRuntime)
	}
	tuples, total, err := stream.ExecuteWithPage(pageIndex, pageSize)
	if err != nil {
		log.GetLogger().Errorf("failed to execute with page: %s", err.Error())
		return model.LayerList{}, err
	}
	layers := make([]model.Layer, len(tuples), len(tuples))
	for i, tuple := range tuples {
		if err := buildLayer(ctx, &layers[i], tuple.Key.LayerName, tuple.Key.LayerVersion, tuple.Value); err != nil {
			log.GetLogger().Errorf("failed to build layer: %s", err.Error())
			return model.LayerList{}, err
		}
	}
	return model.LayerList{
		Total:         total,
		LayerVersions: layers,
	}, nil
}

// GetLayerList handles the request of querying multiple layers
func GetLayerList(ctx server.Context, req model.GetLayerListRequest) (model.LayerList, error) {
	if req.CompatibleRuntime != "" {
		if err := isCompatibleRuntimeValid([]string{req.CompatibleRuntime}); err != nil {
			return model.LayerList{}, err
		}
	}
	if req.OwnerFlag != 1 {
		return model.LayerList{}, snerror.NewWithFmtMsg(0, "ownerflag %v is not implemented", req.OwnerFlag)
	}

	stream, err := storage.GetLayerStream(ctx, "")
	if err != nil {
		log.GetLogger().Errorf("failed to get stream: %s", err.Error())
		return model.LayerList{}, err
	}
	if req.LayerName != "" {
		stream = stream.Filter(func(key storage.LayerKey, val storage.LayerValue) bool {
			return strings.Contains(key.LayerName, req.LayerName)
		})
	}
	if req.LayerVersion != 0 {
		stream = stream.Filter(func(key storage.LayerKey, val storage.LayerValue) bool {
			return key.LayerVersion == req.LayerVersion
		})
	}
	res, err := getLayerListHelper(ctx, stream, req.CompatibleRuntime, req.PageIndex, req.PageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return model.LayerList{}, snerror.New(errmsg.LayerVersionNotFound, "the layer does not exist")
		}
		return model.LayerList{}, err
	}
	return res, nil
}

// GetLayerVersion handles the request of querying a specific layer version
func GetLayerVersion(ctx server.Context, layerName string, layerVersion int) (model.Layer, error) {
	var res model.Layer

	if layerVersion == 0 {
		layer, version, err := storage.GetLayerLatestVersion(ctx, layerName)
		if err != nil {
			if err == errmsg.KeyNotFoundError {
				return model.Layer{}, snerror.New(errmsg.LayerVersionNotFound, "can not find this layer")
			}
			log.GetLogger().Errorf("failed to get layer latest version: %s", err.Error())
			return model.Layer{}, err
		}
		if err := buildLayer(ctx, &res, layerName, version, layer); err != nil {
			log.GetLogger().Errorf("failed to build layer: %s", err)
			return model.Layer{}, err
		}

		return res, nil
	}

	layer, err := storage.GetLayerVersion(ctx, layerName, layerVersion)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return model.Layer{}, snerror.NewWithFmtMsg(
				errmsg.LayerVersionNotFound, "this version [%d] of layer [%s] does not exist",
				layerVersion, layerName)
		}
		log.GetLogger().Errorf("failed to get layer version: %s", err.Error())
		return model.Layer{}, err
	}
	if err := buildLayer(ctx, &res, layerName, layerVersion, layer); err != nil {
		log.GetLogger().Errorf("failed to build layer: %s", err)
		return model.Layer{}, err
	}

	return res, nil
}

// GetLayerVersionList is similar to "GetLayerList". The main difference is that "GetLayerList" fuzzy searches
// layerName.
func GetLayerVersionList(ctx server.Context, req model.GetLayerVersionListRequest) (model.LayerList, error) {
	if req.CompatibleRuntime != "" {
		if err := isCompatibleRuntimeValid([]string{req.CompatibleRuntime}); err != nil {
			return model.LayerList{}, err
		}
	}

	stream, err := storage.GetLayerStream(ctx, req.LayerName)
	if err != nil {
		log.GetLogger().Errorf("failed to get stream: %s", err.Error())
		return model.LayerList{}, err
	}
	res, err := getLayerListHelper(ctx, stream, req.CompatibleRuntime, req.PageIndex, req.PageSize)
	if err != nil {
		if err == errmsg.KeyNotFoundError {
			return model.LayerList{}, snerror.New(errmsg.LayerVersionNotFound, "this layer version does not exist")
		}
		return model.LayerList{}, err
	}
	return res, nil
}

// UpdateLayer handles the request of updating a specific layer version
func UpdateLayer(ctx server.Context, layerName string, req model.UpdateLayerRequest) (model.Layer, error) {
	// start transaction
	txn := storage.NewTxn(ctx)
	defer txn.Cancel()

	layer, err := storage.GetLayerVersionTx(txn, layerName, req.Version)
	if err != nil {
		log.GetLogger().Errorf("failed to get layer latest version: %s", err.Error())
		return model.Layer{}, err
	}
	if !layer.UpdateTime.Equal(req.LastUpdateTime) {
		log.GetLogger().Errorf("check updateTime error, a concurrent update may happen")
		return model.Layer{}, snerror.New(errmsg.RevisionIDError, "last update time is not the same")
	}

	layer.LicenseInfo = req.LicenseInfo
	layer.Description = req.Description
	layer.UpdateTime = time.Now()

	if err := storage.UpdateLayer(txn, layerName, req.Version, layer); err != nil {
		log.GetLogger().Errorf("failed to update layer: %s", err.Error())
		return model.Layer{}, err
	}
	if err := txn.Commit(); err != nil {
		log.GetLogger().Errorf("failed to commit: %s", err.Error())
		return model.Layer{}, err
	}

	var res model.Layer
	if err := buildLayer(ctx, &res, layerName, req.Version, layer); err != nil {
		log.GetLogger().Errorf("failed to build layer: %s", err)
		return model.Layer{}, err
	}
	return res, nil
}
