/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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

package metadata

// CodeMetaData define code meta info
type CodeMetaData struct {
	// for repo manage package if local means package not controlled by repo
	CodeUploadType string `json:"codeUploadType" valid:",optional"`
	Sha512         string `json:"sha512" valid:"optional"`
	LocalMetaData
	S3MetaData
}

// S3MetaData define meta function info for OBS
type S3MetaData struct {
	AppID        string   `json:"appId" valid:"stringlength(1|128),optional"`
	BucketID     string   `json:"bucketId" valid:"stringlength(1|255),optional"`
	ObjectID     string   `json:"objectId" valid:"stringlength(1|255),optional"`
	BucketURL    string   `json:"bucketUrl" valid:"url,optional"`
	CodeType     string   `json:"code_type" valid:",optional"`
	CodeURL      string   `json:"code_url" valid:",optional"`
	CodeFileName string   `json:"code_filename" valid:",optional"`
	FuncCode     FuncCode `json:"func_code" valid:",optional"`
}

// LocalMetaData define meta function info for local
type LocalMetaData struct {
	StorageType string `json:"storage_type" valid:",optional"`
	CodePath    string `json:"code_path" valid:"optional"`
}
