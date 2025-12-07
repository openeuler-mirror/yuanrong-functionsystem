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
	"meta_service/function_repo/config"
)

type noopChooser struct{}

// Choose implements BucketChooser
func (noopChooser) Choose(businessID string) (string, error) {
	return "", nil
}

// Find implements BucketChooser
func (noopChooser) Find(businessID string, bucketID string) (config.BucketConfig, error) {
	return config.BucketConfig{}, nil
}

type noopManager struct{}

// Delete implements Manager
func (noopManager) Delete(bucketName, objectName string) error {
	return nil
}
