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
	"meta_service/function_repo/server"
)

type noopUploaderBuilder struct{}

// NewUploader implements UploaderBuilder
func (noopUploaderBuilder) NewUploader(c server.Context, pkg Package) (Uploader, error) {
	return noopUploader{}, nil
}

type noopUploader struct{}

// BucketID implements Uploader
func (noopUploader) BucketID() string {
	return ""
}

// ObjectID implements Uploader
func (noopUploader) ObjectID() string {
	return ""
}

// Upload implements Uploader
func (noopUploader) Upload() error {
	return nil
}

// Rollback implements Uploader
func (noopUploader) Rollback() error {
	return nil
}
