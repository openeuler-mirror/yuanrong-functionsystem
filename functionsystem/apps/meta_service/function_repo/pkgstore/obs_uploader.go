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

package pkgstore

import (
	"github.com/huaweicloud/huaweicloud-sdk-go-obs/obs"

	"meta_service/common/logger/log"
	"meta_service/common/reader"
	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
)

const obsBucketNotExistCode = 404

type s3UploaderBuilder struct {
	cli                    *obs.ObsClient
	createBucketIfNotExist bool
	chooser                BucketChooser
}

func newS3UploaderBuilder(
	cli *obs.ObsClient, createBucketIfNotExist bool, chooser BucketChooser) UploaderBuilder {

	res := &s3UploaderBuilder{
		cli:                    cli,
		createBucketIfNotExist: createBucketIfNotExist,
		chooser:                chooser,
	}
	return res
}

// NewUploader implements PackageBuilder
func (b *s3UploaderBuilder) NewUploader(c server.Context, pkg Package) (Uploader, error) {
	tenant, err := c.TenantInfo()
	if err != nil {
		return nil, err
	}

	bucketName, err := b.chooser.Choose(tenant.BusinessID)
	if err != nil {
		return nil, err
	}

	return &s3Uploader{
		cli:                    b.cli,
		bucketName:             bucketName,
		objectName:             pkg.Name(),
		filePath:               pkg.FileName(),
		createBucketIfNotExist: b.createBucketIfNotExist,
	}, nil
}

type s3Uploader struct {
	cli                    *obs.ObsClient
	bucketName             string
	objectName             string
	filePath               string
	createBucketIfNotExist bool
}

// BucketID implements Uploader
func (u *s3Uploader) BucketID() string {
	return u.bucketName
}

// ObjectID implements Uploader
func (u *s3Uploader) ObjectID() string {
	return u.objectName
}

func (u *s3Uploader) createBucket(err error) error {
	if obsError, ok := err.(obs.ObsError); ok {
		if obsError.StatusCode == obsBucketNotExistCode {
			log.GetLogger().Infof("bucket %s does not exist", u.bucketName)
			_, err := u.cli.CreateBucket(&obs.CreateBucketInput{
				Bucket: u.bucketName,
			})
			if err != nil {
				log.GetLogger().Errorf("failed to create bucket in S3, "+
					"bucketName: %s, objectName: %s, uploadFilePath: %s: %s",
					u.bucketName, u.objectName, u.filePath, err.Error())
				return snerror.NewWithError(errmsg.UploadFileError, err)
			}
		} else {
			log.GetLogger().Errorf("failed to check bucket exists in S3, "+
				"bucketName: %s, objectName: %s, uploadFilePath: %s: %s",
				u.bucketName, u.objectName, u.filePath, err.Error())
			return snerror.NewWithError(errmsg.UploadFileError, err)
		}
	} else {
		return snerror.NewWithError(errmsg.UploadFileError, err)
	}
	return nil
}

// Upload implements Uploader
func (u *s3Uploader) Upload() error {
	if u.createBucketIfNotExist {
		_, err := u.cli.HeadBucket(u.bucketName)
		if err != nil {
			err := u.createBucket(err)
			if err != nil {
				return err
			}
		}
	}
	fileInfo, err := reader.ReadFileInfoWithTimeout(u.filePath)
	if err != nil {
		log.GetLogger().Errorf("failed to get file size. path: %s, error: %s",
			u.filePath, err.Error())
		return snerror.NewWithError(errmsg.UploadFileError, err)
	}

	fileInput := &obs.PutFileInput{}
	fileInput.Bucket = u.bucketName
	fileInput.Key = u.objectName
	fileInput.SourceFile = u.filePath
	fileInput.ContentLength = fileInfo.Size()
	_, err = u.cli.PutFile(fileInput)
	if err != nil {
		log.GetLogger().Errorf("failed to writing to S3, "+
			"bucketName: %s, objectName: %s, uploadFilePath: %s: %s",
			u.bucketName, u.objectName, u.filePath, err.Error())
		return snerror.NewWithError(errmsg.UploadFileError, err)
	}
	return nil
}

// Rollback implements Uploader
func (u *s3Uploader) Rollback() error {
	deleteParam := &obs.DeleteObjectInput{}
	deleteParam.Bucket = u.bucketName
	deleteParam.Key = u.objectName
	_, err := u.cli.DeleteObject(&obs.DeleteObjectInput{
		Bucket: u.bucketName,
		Key:    u.objectName,
	})
	if err != nil {
		log.GetLogger().Errorf("failed to delete object from S3, "+
			"bucketName: %s, objectName: %s, uploadFilePath: %s: %s",
			u.bucketName, u.objectName, u.filePath, err.Error())
		return snerror.NewWithError(errmsg.DeleteFileError, err)
	}
	return nil
}
