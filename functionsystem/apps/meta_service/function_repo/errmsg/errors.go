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

// Package errmsg defines error messages
package errmsg

import (
	"meta_service/common/snerror"
)

const (
	descriptionSeparator = ". "

	// FunctionNameExist means function name exist
	FunctionNameExist = 4101
	// AliasNameAlreadyExists means alias name exists
	AliasNameAlreadyExists = 4102
	// TotalRoutingWeightNotOneHundred means total routing weight is not 100
	TotalRoutingWeightNotOneHundred = 4103
	// InvalidUserParam means user's input parameter is not valid
	InvalidUserParam = 4104
	// FunctionVersionDeletionForbidden means function version has relate alias, so cannot be deleted
	FunctionVersionDeletionForbidden = 4105
	// LayerVersionSizeOutOfLimit means layer version is out of limit
	LayerVersionSizeOutOfLimit = 4106
	// TenantLayerSizeOutOfLimit means layer size within the tenant is out of limit
	TenantLayerSizeOutOfLimit = 4107
	// LayerVersionNumOutOfLimit means layer version number is out of limit
	LayerVersionNumOutOfLimit = 4108
	// TriggerNumOutOfLimit means number of triggers of the function is out of limit
	TriggerNumOutOfLimit = 4109
	// FunctionVersionOutOfLimit means function version exceeds limit
	FunctionVersionOutOfLimit = 4110
	// AliasOutOfLimit means alias number of function exceeds limit
	AliasOutOfLimit = 4111
	// LayerIsUsed means layer if used by function, so cannot be deleted
	LayerIsUsed = 4112
	// LayerVersionNotFound means layer version is not created
	LayerVersionNotFound = 4113
	// RepeatedPublishmentError means there is no difference between two publishment
	RepeatedPublishmentError = 4114
	// FunctionNotFound means function is not created or is deleted
	FunctionNotFound = 4115
	// FunctionVersionNotFound means function version is not created or is deleted
	FunctionVersionNotFound = 4116
	// AliasNameNotFound means alias is not created or is deleted
	AliasNameNotFound = 4117
	// TriggerNotFound means trigger is not created or is deleted
	TriggerNotFound = 4118
	// LayerNotFound means layer is not created or is deleted
	LayerNotFound = 4119
	// PoolNotFound means pool is not created or is deleted
	PoolNotFound = 4120

	// PageInfoError means page info out of list boundary
	PageInfoError = 4123
	// TriggerPathRepeated means trigger's path is used by other trigger
	TriggerPathRepeated = 4124
	// DuplicateCompatibleRuntimes means duplicate items found in compatible runtimes list
	DuplicateCompatibleRuntimes = 4125
	// CompatibleRuntimeError means some items of compatible runtimes are not in configuration list
	CompatibleRuntimeError = 4126
	// ZipFileCountError means count of files inside zip file is out of limit of configuration
	ZipFileCountError = 4127
	// ZipFilePathError means some file path inside zip file is not valid
	ZipFilePathError = 4128
	// ZipFileUnzipSizeError means unzipped files size of zip file is out of limit of configuration
	ZipFileUnzipSizeError = 4129
	// ZipFileSizeError means size of zip file is out of limit of configuration
	ZipFileSizeError = 4130

	// RevisionIDError means revision ID of request does not match that of entry to operate in storage
	RevisionIDError = 4134

	// SaveFileError means saving temporary file has some error when handling zip file of package
	SaveFileError = 121016
	// UploadFileError means uploading file has some error when handling zip file of package
	UploadFileError = 121017
	// EmptyAliasAndVersion means alias name and version are empty
	EmptyAliasAndVersion = 121018
	// ReadingPackageTimeout means timeout while reading a package
	ReadingPackageTimeout = 121019

	// BucketNotFound means bucket info is not found for specific business ID
	BucketNotFound = 121026
	// ZipFileError means error occurs when handling zip file of package
	ZipFileError = 121029
	// DownloadFileError means error occurs when getting presigned downloading URL from package storage
	DownloadFileError = 121030
	// DeleteFileError means error occurs when deleting object of package storage
	DeleteFileError = 121032
	// InvalidFunctionLayer means layer's tenant info is not match that of its request's context
	InvalidFunctionLayer = 121036

	// InvalidQueryURN means URN in query request is invalid
	InvalidQueryURN = 121046
	// InvalidQualifier means qualifier in request is invalid, it represents either version or alias
	InvalidQualifier = 121047
	// ReadBodyError means error occurs when handling HTTP request's body
	ReadBodyError = 121048
	// AuthCheckError means error occurs when authentication HTTP request
	AuthCheckError = 121049
	// InvalidJSONBody means error occurs when parsing HTTP request's body to JSON format
	InvalidJSONBody = 121052
	// InvalidParamErrorCode means error occurs when parsing HTTP request's body to JSON format
	InvalidParamErrorCode = 130600
	// TriggerIDNotFound means trigger ID in request is not found in storage
	TriggerIDNotFound = 121057
	// FunctionNameFormatErr means function name format is wrong in storage
	FunctionNameFormatErr = 121058

	// KVNotFound means etcd has no such kv
	KVNotFound = 122001
	// EtcdError means an error in etcd
	EtcdError = 122002
	// TransactionFailed means an etcd transaction has an error
	TransactionFailed = 122003
	// UnmarshalFailed means unmarshal to json error
	UnmarshalFailed = 122004
	// MarshalFailed means marshal to string error
	MarshalFailed = 122005
	// VersionOrAliasEmpty means version or alias is empty string
	VersionOrAliasEmpty = 122006
	// ResourceIDEmpty means resource ID is empty string
	ResourceIDEmpty = 122007
	// NoTenantInfo means tenant info does not exist
	NoTenantInfo = 122008
	// NoResourceInfo means resource info does not exist
	NoResourceInfo = 122009
)

var checkInputParamMsg = "check input parameters"

var errorMsg = map[int]string{
	InvalidUserParam:        userMessage("%s", checkInputParamMsg),
	FunctionNameExist:       userMessage("the function name already exists", "rename your function"),
	RevisionIDError:         "revisionID is not the same",
	FunctionVersionNotFound: userMessage("function [%s] version [%s] is not found", checkInputParamMsg),
	FunctionVersionDeletionForbidden: userMessage("this version is occupied by an alias", "remove the "+
		"mapping first"),
	FunctionNotFound: userMessage("function [%s] is not found", checkInputParamMsg),
	LayerVersionSizeOutOfLimit: userMessage("the version size [%d] is larger than max config size [%d]",
		"delete or resize"),
	TenantLayerSizeOutOfLimit: userMessage("the tenant layer size [%d] is larger than the maximum config size [%d"+
		"]", "consult the administrator"),
	LayerVersionNumOutOfLimit: userMessage("the maximum layer version number is larger than the config num [%d]",
		checkInputParamMsg),
	LayerIsUsed: userMessage("layer version %d has been bound to the function",
		"please unbind it before performing the operation"),
	PageInfoError:               userMessage("the page information is out of the query range", checkInputParamMsg),
	LayerNotFound:               userMessage("layer [%s] not found", checkInputParamMsg),
	DuplicateCompatibleRuntimes: userMessage("duplicated compatibleRuntimes exists", checkInputParamMsg),
	CompatibleRuntimeError:      userMessage("compatible runtime is invalid", checkInputParamMsg),
	TriggerPathRepeated:         userMessage("trigger path is repeated", checkInputParamMsg),
	TriggerNumOutOfLimit: userMessage("the number of triggers exceeds the upper limit [%d]",
		"delete or resize the limit"),
	TriggerNotFound:   userMessage("trigger [%s] is not found", checkInputParamMsg),
	TriggerIDNotFound: userMessage("trigger [%s] is not found", checkInputParamMsg),
	AliasNameNotFound: userMessage("functionName [%s] and aliasName [%s] do not exist", checkInputParamMsg),
	AliasOutOfLimit: userMessage("the number of existing function alias is greater than the set value [%d]",
		"delete or resize the limit"),
	InvalidJSONBody: "request body is not a valid JSON object",
	TotalRoutingWeightNotOneHundred: userMessage("total routing weight is not 100",
		"check the routing weight. the sum of the values is 100"),
}

var (
	// KeyNotFoundError means key does not exist in etcd
	KeyNotFoundError = snerror.New(KVNotFound, "KV not exist in etcd")
	// EtcdInternalError means etcd has some error
	EtcdInternalError = snerror.New(EtcdError, "etcd internal error")
	// EtcdTransactionFailedError means etcd transaction has some error
	EtcdTransactionFailedError = snerror.New(TransactionFailed, "etcd transaction failed")
	// UnmarshalError means unmarshal to json error
	UnmarshalError = snerror.New(UnmarshalFailed, "failed to unmarshal")
	// MarshalError means marshal to string error
	MarshalError = snerror.New(MarshalFailed, "failed to marshal")
	// VersionOrAliasError means version or alias is empty string
	VersionOrAliasError = snerror.New(VersionOrAliasEmpty, "version or alias name is empty")
	// ResourceIDError means resource ID is empty string
	ResourceIDError = snerror.New(ResourceIDEmpty, "resource ID is empty")
	// PageError means page info out of list boundary
	PageError = snerror.New(PageInfoError, "page size cannot be 0")
	// PoolNotFoundError means pool id is not found
	PoolNotFoundError = snerror.New(PoolNotFound, userMessage("pool does not exist or has been deleted",
		checkInputParamMsg))
)

// ErrorMessage returns a text for the error code. It returns the empty
// string if the code is unknown.
func ErrorMessage(code int) string {
	return errorMsg[code]
}

// userMessage constructs user message which composes of two parts: message and suggestion
func userMessage(msg string, suggestions string) string {
	return msg + descriptionSeparator + suggestions
}

// New generates an error with code and format
func New(code int, v ...interface{}) snerror.SNError {
	return snerror.NewWithFmtMsg(code, ErrorMessage(code), v...)
}

// NewParamError generates an error of type InvalidUserParam with specified error message.
func NewParamError(msg string, v ...interface{}) snerror.SNError {
	return snerror.NewWithFmtMsg(InvalidUserParam, userMessage(msg, checkInputParamMsg), v...)
}
