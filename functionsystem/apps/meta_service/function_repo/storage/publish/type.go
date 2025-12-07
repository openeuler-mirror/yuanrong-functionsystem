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

// Package publish implements an event publisher to publish create, update and delete events of functions,
// triggers and aliases.
package publish

// AliasEtcd alias info was watched by workerManager etc.
type AliasEtcd struct {
	AliasURN           string `json:"aliasUrn"`
	FunctionURN        string `json:"functionUrn"`
	FunctionVersionURN string `json:"functionVersionUrn"`
	Name               string `json:"name"`
	FunctionVersion    string `json:"functionVersion"`
	RevisionID         string `json:"revisionId"`
	Description        string `json:"description"`
	// original java code use "reoutingconfig" as json key
	RoutingConfig []AliasRoutingEtcd `json:"routingconfig"`
}

// AliasRoutingEtcd -
type AliasRoutingEtcd struct {
	FunctionVersionURN string `json:"functionVersionUrn"`
	Weight             int    `json:"weight"`
}
