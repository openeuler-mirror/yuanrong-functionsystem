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

// Package functiontypes contains function info stored on etcd
package functiontypes

// ResourceMetaData include resource data such as cpu and memory
type ResourceMetaData struct {
	CPU             int    `json:"cpu"`
	Memory          int    `json:"memory"`
	CustomResources string `json:"customResources"`
}

// LayerBucket -
type LayerBucket struct {
	BucketURL string `json:"bucketUrl"`
	ObjectID  string `json:"objectId"`
	BucketID  string `json:"bucketId"`
	AppID     string `json:"appId"`
	Sha256    string `json:"sha256"`
}

// ExtendedMetaData -
type ExtendedMetaData struct {
	UserType         string            `json:"user_type" valid:"optional"`
	InstanceMetaData map[string]int    `json:"instance_meta_data" valid:"optional"`
	ExtendedHandler  map[string]string `json:"extended_handler" valid:",optional"`
	ExtendedTimeout  map[string]int    `json:"extended_timeout" valid:",optional"`
}
