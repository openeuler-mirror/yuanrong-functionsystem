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
	"crypto/rand"
	"math/big"
	"time"

	"github.com/huaweicloud/huaweicloud-sdk-go-obs/obs"

	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
)

type obsBucketChooser struct {
	m map[string][]config.BucketConfig
}

func newObsBucketChooser(buckets []config.BucketConfig) BucketChooser {
	res := &obsBucketChooser{
		m: make(map[string][]config.BucketConfig, len(buckets)),
	}
outer:
	for _, b := range buckets {
		v, exist := res.m[b.BusinessID]
		if !exist {
			res.m[b.BusinessID] = []config.BucketConfig{b}
			continue
		}
		// skip duplicates
		for _, bb := range v {
			if bb.BucketID == b.BucketID {
				continue outer
			}
		}

		v = append(v, b)
		res.m[b.BusinessID] = v
	}
	return res
}

// Choose implements BucketChooser
func (c *obsBucketChooser) Choose(businessID string) (string, error) {
	v, ok := c.m[businessID]
	if !ok {
		log.GetLogger().Errorf("failed to find bucket for business id %s", businessID)
		return "", snerror.NewWithFmtMsg(
			errmsg.BucketNotFound, "failed to find bucket for business id %s", businessID)
	}
	res := v[getRandomNum(len(v))].BucketID
	return res, nil
}

func getRandomNum(n int) int {
	randomIndex, err := rand.Int(rand.Reader, big.NewInt(int64(n)))
	if err != nil {
		log.GetLogger().Errorf("failed to generate random num %s", err.Error())
		return 0
	}
	return int(randomIndex.Int64())
}

// Find implements BucketChooser
func (c *obsBucketChooser) Find(businessID string, bucketID string) (config.BucketConfig, error) {
	v, ok := c.m[businessID]
	if !ok {
		log.GetLogger().Errorf("failed to find bucket for business id %s", businessID)
		return config.BucketConfig{}, snerror.NewWithFmtMsg(
			errmsg.BucketNotFound, "failed to find bucket for business id %s", businessID)
	}
	for _, conf := range v {
		if conf.BucketID == bucketID {
			return conf, nil
		}
	}
	log.GetLogger().Errorf("failed to find bucket for business id %s and bucket id %s", businessID, bucketID)
	return config.BucketConfig{}, snerror.NewWithFmtMsg(
		errmsg.BucketNotFound, "failed to find bucket for business id %s and bucket id %s", businessID, bucketID)
}

type s3Manager struct {
	cli        *obs.ObsClient
	urlExpires time.Duration
}

func newS3Manager(cli *obs.ObsClient, urlExpires time.Duration) *s3Manager {
	return &s3Manager{
		cli:        cli,
		urlExpires: urlExpires,
	}
}

// Delete implements Manager
func (m *s3Manager) Delete(bucketName, objectName string) error {
	if bucketName == "" || objectName == "" {
		log.GetLogger().Warnf("deleting with empty bucketName or empty objectName")
		return nil
	}

	deleteParam := &obs.DeleteObjectInput{
		Bucket: bucketName,
		Key:    objectName,
	}
	_, err := m.cli.DeleteObject(deleteParam)
	if err != nil {
		log.GetLogger().Errorf("failed to delete object url from obs with bucketName %s, objectName %s: %s",
			bucketName, objectName, err.Error())
		return snerror.NewWithError(errmsg.DeleteFileError, err)
	}
	return nil
}
