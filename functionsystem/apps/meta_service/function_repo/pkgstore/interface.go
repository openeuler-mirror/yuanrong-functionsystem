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

// Package pkgstore handles the upload and deletion of function code packages and layer packages
package pkgstore

import (
	"errors"
	"io"
	"time"

	"meta_service/common/logger/log"
	commonObs "meta_service/common/obs"
	"meta_service/function_repo/config"
	"meta_service/function_repo/server"
)

const (
	// defaultMaxRetryCount default obs max retry count
	defaultMaxRetryCount = 0
)

// PackageBuilder can create a new package.
type PackageBuilder interface {
	// NewPackage creates a new package. name is the package name. reader is the input byte stream, n is reader's
	// expected length.
	NewPackage(c server.Context, name string, reader io.Reader, n int64) (Package, error)
}

// Package represents a function code package or a layer package.
type Package interface {
	// Name is the package name.
	Name() string

	// FileName is the file of a package.
	FileName() string

	// Move gives up its ownership of the underlying file and returns the file name.
	Move() string

	// Signature returns a hash value that identifies the package. A typical signature algorithm is sha256.
	Signature() string

	// Size returns the package file size.
	Size() int64

	// Close cleans up temporary resources holds by the package.
	Close() error
}

// UploaderBuilder can create a new uploader
type UploaderBuilder interface {
	NewUploader(c server.Context, pkg Package) (Uploader, error)
}

// Uploader is responsible for uploading a package.
type Uploader interface {
	BucketID() string
	ObjectID() string

	// Upload uploads the package. In case of error, user can call Rollback to undo.
	Upload() error

	// Rollback undoes the upload.
	Rollback() error
}

// BucketChooser implements strategies choosing buckets for different businesses.
type BucketChooser interface {
	Choose(businessID string) (string, error)
	Find(businessID string, bucketID string) (config.BucketConfig, error)
}

// Manager manages packages
type Manager interface {
	Delete(bucketName, objectName string) error
}

var (
	defaultPackageBuilder  PackageBuilder
	defaultUploaderBuilder UploaderBuilder = noopUploaderBuilder{}
	defaultBucketChooser   BucketChooser   = noopChooser{}
	defaultManager         Manager         = noopManager{}
)

// Init inits the package. User should only call it ONCE.
func Init() error {
	var err error
	defaultPackageBuilder, err = newZipPackageBuilder(config.RepoCfg.FunctionCfg.PackageCfg)
	if err != nil {
		log.GetLogger().Errorf("failed to create zip package builder: %s", err.Error())
		return err
	}

	switch config.RepoCfg.FileServer.StorageType {
	case "local":
		return nil
	case "s3":
		obsCfg := config.RepoCfg.FileServer.S3
		conf := commonObs.Option{
			Secure:        obsCfg.Secure,
			AccessKey:     obsCfg.AccessKey,
			SecretKey:     obsCfg.SecretKey,
			Endpoint:      obsCfg.Endpoint,
			CaFile:        obsCfg.CaFile,
			TrustedCA:     obsCfg.TrustedCA,
			MaxRetryCount: defaultMaxRetryCount,
			Timeout:       int(time.Duration(obsCfg.Timeout)),
		}
		cli, err := commonObs.NewObsClient(conf)
		commonObs.ClearSecretKeyMemory()
		if err != nil {
			return err
		}

		defaultBucketChooser = newObsBucketChooser(config.RepoCfg.BucketCfg)
		defaultUploaderBuilder = newS3UploaderBuilder(cli, true, defaultBucketChooser)
		defaultManager = newS3Manager(cli, time.Duration(obsCfg.URLExpires))
		return nil
	default:
		return errors.New("unknown storage type")
	}
}

// NewPackage calls defaultPackageBuilder.NewPackage.
func NewPackage(c server.Context, name string, reader io.Reader, n int64) (Package, error) {
	return defaultPackageBuilder.NewPackage(c, name, reader, n)
}

// NewUploader calls defaultUploaderBuilder.NewUploader.
func NewUploader(c server.Context, pkg Package) (Uploader, error) {
	return defaultUploaderBuilder.NewUploader(c, pkg)
}

// FindBucket calls defaultBucketChooser.Find.
func FindBucket(businessID string, bucketID string) (config.BucketConfig, error) {
	return defaultBucketChooser.Find(businessID, bucketID)
}

// Delete calls defaultManager.Delete.
func Delete(bucketID, objectID string) error {
	return defaultManager.Delete(bucketID, objectID)
}
