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

package storage

import (
	"encoding/json"
	"io"
	"testing"
	"time"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/test/fakecontext"

	"meta_service/common/engine"
	"meta_service/common/snerror"

	"github.com/golang/mock/gomock"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
)

func getStorage(ctrl *gomock.Controller) (*engine.MockEngine, *engine.MockTransaction, *Txn) {
	transaction := engine.NewMockTransaction(ctrl)
	eng := engine.NewMockEngine(ctrl)
	eng.EXPECT().BeginTx(gomock.Any()).Return(transaction)
	_ = InitStorageByEng(eng, "test")

	ctx := fakecontext.NewMockContext()
	txn := NewTxn(ctx)
	return eng, transaction, txn
}

func getTriggerValue(triggerType string) TriggerValue {
	triggerValue := TriggerValue{
		TriggerID:   "242cbae7-8c50-4af0-8e35-9d4f113ca94a",
		FuncName:    "0-base-hellopy",
		TriggerType: triggerType,
		RevisionID:  "20220228022355085",
		CreateTime:  time.Now(),
		UpdateTime:  time.Now(),
	}
	if triggerType == "HTTP" {
		triggerValue.EtcdSpec = &HTTPTriggerEtcdSpec{
			HTTPMethod:    "POST",
			ResourceID:    "api",
			AuthFlag:      false,
			AuthAlgorithm: "HMAC_SHA256",
			TriggerURL:    "http://127.0.0.1:31220/service/cn:yrk:function:907ba668fd57:0-base-hellopy:$latest/api",
			AppID:         "yrk",
			AppSecret:     "12CFV18835434FDGEEF39BD6YRE45D46",
		}
	}
	return triggerValue
}

func TestSaveTriggerInfoHTTP(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	triggerValue := getTriggerValue("HTTP")
	put1 := tran.EXPECT().Put(gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/aaa/"), gomock.Any())
	put2 := tran.EXPECT().Put(gomock.Eq("test/TriggerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//aaa/"), gomock.Any())
	tran.EXPECT().Get(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/api/")).Return("", engine.ErrKeyNotFound)
	put3 := tran.EXPECT().Put(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/api/"), gomock.Eq("{}"))
	gomock.InOrder(put1, put2, put3)
	if err := SaveTriggerInfo(txn, "funcName", "alias", "aaa", triggerValue); err != nil {
		t.Fatalf(err.Error())
	}
}

func TestDeleteTriggerHTTP(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)
	triggerRaw, err := json.Marshal(getTriggerValue("HTTP"))
	if err != nil {
		t.Fatalf(err.Error())
	}

	tran.EXPECT().Get(gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/aaa/")).Return(string(triggerRaw[:]), nil)
	tran.EXPECT().Del(gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/aaa/"))
	tran.EXPECT().Del(gomock.Eq("test/TriggerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//aaa/"))
	tran.EXPECT().Del(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/api/"))

	if err := DeleteTrigger(txn, "funcName", "alias", "aaa"); err != nil {
		t.Fatalf(err.Error())
	}
}

func TestCheckResourceIDExist(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()

	_, tran, txn := getStorage(ctrl)
	res, err := CheckResourceIDExist(txn, "funcName", "", "resourceID")
	assert.False(t, res)
	assert.Equal(t, errmsg.VersionOrAliasError, err)

	res, err = CheckResourceIDExist(txn, "funcName", "alias", "")
	assert.False(t, res)
	assert.Equal(t, errmsg.ResourceIDError, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/resourceID/")).Return("", engine.ErrKeyNotFound)
	res, err = CheckResourceIDExist(txn, "funcName", "alias", "resourceID")
	assert.False(t, res)
	assert.Nil(t, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/resourceID/")).Return("", errmsg.EtcdInternalError)
	res, err = CheckResourceIDExist(txn, "funcName", "alias", "resourceID")
	assert.False(t, res)
	assert.Equal(t, errmsg.EtcdInternalError, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionResourceIDIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/resourceID/")).Return("{}", nil)
	res, err = CheckResourceIDExist(txn, "funcName", "alias", "resourceID")
	assert.True(t, res)
	assert.Nil(t, err)
}

func TestCreateAlias(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	tran.EXPECT().Put(gomock.Eq("test/Alias/yrk/i1fe539427b24702acc11fbb4e134e17//bbb/aaa/"), gomock.Any())
	tran.EXPECT().Put(gomock.Eq("test/AliasRoutingIndex/yrk/i1fe539427b24702acc11fbb4e134e17//bbb/1/"), gomock.Any())
	tran.EXPECT().Put(gomock.Eq("test/AliasRoutingIndex/yrk/i1fe539427b24702acc11fbb4e134e17//bbb/2/"), gomock.Any())
	v := AliasValue{
		Name:            "aaa",
		FunctionName:    "bbb",
		FunctionVersion: "$latest",
		RevisionID:      "ccc",
		Description:     "",
		RoutingConfig:   map[string]int{"1": 20, "2": 80},
		CreateTime:      time.Now(),
		UpdateTime:      time.Now(),
	}
	err := CreateAlias(txn, v)
	assert.Nil(t, err)
}

func TestDeleteAlias(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	tran.EXPECT().Del(gomock.Eq("test/Alias/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/alias/"))
	tran.EXPECT().Del(gomock.Eq("test/AliasRoutingIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/1/"))
	tran.EXPECT().Del(gomock.Eq("test/AliasRoutingIndex/yrk/i1fe539427b24702acc11fbb4e134e17//funcName/2/"))
	err := DeleteAlias(txn, "funcName", "alias", []string{"1", "2"})
	assert.Nil(t, err)
}

func TestIsObjectReferred(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)
	res, err := IsObjectReferred(txn, "", "")
	assert.False(t, res)
	assert.Nil(t, err)

	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17//bucket/object/")).Return("{\"RefCnt\":1}", nil)
	res, err = IsObjectReferred(txn, "bucket", "object")
	assert.True(t, res)

	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17//bucket/object/")).Return("", engine.ErrKeyNotFound)
	res, err = IsObjectReferred(txn, "bucket", "object")
	assert.False(t, res)
	assert.Nil(t, err)
}

func TestGetAliasesByPage(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	eng := engine.NewMockEngine(ctrl)
	stream := engine.NewMockStream(ctrl)
	stmt := engine.NewMockPrepareStmt(ctrl)

	_ = InitStorageByEng(eng, "test")
	ctx := fakecontext.NewMockContext()

	// key not found
	stream.EXPECT().Next().Return(nil, io.EOF)
	stmt.EXPECT().Execute().Return(stream, nil)
	eng.EXPECT().PrepareStream(gomock.Any(), gomock.Eq("test/Alias/yrk/i1fe539427b24702acc11fbb4e134e17//aaa/"), gomock.Any(), gomock.Any()).Return(stmt)
	va, err := GetAliasesByPage(ctx, "aaa", 0, 10)
	assert.NoError(t, err)
	assert.Equal(t, 0, len(va))

	// other err
	stream.EXPECT().Next().Return(nil, errmsg.EtcdInternalError)
	stmt.EXPECT().Execute().Return(stream, nil)
	eng.EXPECT().PrepareStream(gomock.Any(), gomock.Eq("test/Alias/yrk/i1fe539427b24702acc11fbb4e134e17//aaa/"), gomock.Any(), gomock.Any()).Return(stmt)
	va, err = GetAliasesByPage(ctx, "aaa", 0, 10)
	assert.Error(t, err)

	// get one
	stream.EXPECT().Next().Return(AliasTuple{}, nil)
	stream.EXPECT().Next().Return(nil, io.EOF)
	stmt.EXPECT().Execute().Return(stream, nil)
	eng.EXPECT().PrepareStream(gomock.Any(), gomock.Eq("test/Alias/yrk/i1fe539427b24702acc11fbb4e134e17//aaa/"), gomock.Any(), gomock.Any()).Return(stmt)
	va, err = GetAliasesByPage(ctx, "aaa", 0, 10)
	assert.NoError(t, err)
	assert.Equal(t, 1, len(va))
}

func TestGetTriggerInfoList(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	eng := engine.NewMockEngine(ctrl)
	stream := engine.NewMockStream(ctrl)
	stmt := engine.NewMockPrepareStmt(ctrl)

	_ = InitStorageByEng(eng, "test")
	ctx := fakecontext.NewMockContext()

	stream.EXPECT().Next().Return(nil, errmsg.EtcdInternalError)
	stmt.EXPECT().Execute().Return(stream, nil)
	eng.EXPECT().PrepareStream(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/"), gomock.Any(), gomock.Any()).Return(stmt)
	_, err := GetTriggerInfoList(ctx, "aa", "bb", 0, 1)
	assert.Error(t, err)

	// get one
	stream.EXPECT().Next().Return(TriggerTuple{}, nil)
	stream.EXPECT().Next().Return(nil, io.EOF)
	stmt.EXPECT().Execute().Return(stream, nil)
	eng.EXPECT().PrepareStream(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/"), gomock.Any(), gomock.Any()).Return(stmt)
	va, err := GetTriggerInfoList(ctx, "aa", "bb", 0, 1)
	assert.NoError(t, err)
	assert.Equal(t, 1, len(va))
}

func TestGetTriggerByFunctionNameVersion(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	tran.EXPECT().GetPrefix(gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return(nil, nil, errmsg.EtcdInternalError)
	_, err := GetTriggerByFunctionNameVersion(txn, "aa", "bb")
	assert.Error(t, err)

	tran.EXPECT().GetPrefix(gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return([]string{}, []string{}, nil)
	va, err := GetTriggerByFunctionNameVersion(txn, "aa", "bb")
	assert.NoError(t, err)
	assert.Equal(t, 0, len(va))
}

func TestGetTriggerInfoByTriggerID(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	eng := engine.NewMockEngine(ctrl)

	_ = InitStorageByEng(eng, "test")
	ctx := fakecontext.NewMockContext()

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/cc/")).Return(`{"TriggerID":"","FuncName":"","TriggerType":"HTTP","RevisionID":"","EtcdSpec":{"HTTPMethod":"","ResourceID":"","AuthFlag":false,"AuthAlgorithm":"","TriggerURL":"","AppID":"","AppSecret":""},"CreateTime":"2022-04-02T09:01:06.323986292Z","UpdateTime":"2022-04-02T09:01:06.323986454Z"}`, nil)
	_, err := GetTriggerInfoByTriggerID(ctx, "aa", "bb", "cc")
	assert.NoError(t, err)

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/cc/")).Return("{}", nil)
	_, err = GetTriggerInfoByTriggerID(ctx, "aa", "bb", "cc")
	assert.Error(t, err)
	assert.Equal(t, errmsg.UnmarshalFailed, err.(snerror.SNError).Code())

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/cc/")).Return("{}", engine.ErrKeyNotFound)
	_, err = GetTriggerInfoByTriggerID(ctx, "aa", "bb", "cc")
	assert.Error(t, err)
	assert.Equal(t, errmsg.TriggerIDNotFound, err.(snerror.SNError).Code())

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/Trigger/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/cc/")).Return("{}", engine.ErrTransaction)
	_, err = GetTriggerInfoByTriggerID(ctx, "aa", "bb", "cc")
	assert.Error(t, err)
	assert.Equal(t, errmsg.EtcdError, err.(snerror.SNError).Code())
}

func TestGetFunctionInfoByTriggerID(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	eng := engine.NewMockEngine(ctrl)

	_ = InitStorageByEng(eng, "test")
	ctx := fakecontext.NewMockContext()

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/TriggerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//aa/")).Return(`{"FunctionName":"aa","VersionOrAlias":"bb"}`, nil)
	_, err := GetFunctionInfoByTriggerID(ctx, "aa")
	assert.NoError(t, err)

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/TriggerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//aa/")).Return(`{"FunctionName":"aa","VersionOrAlias":"bb"}`, engine.ErrKeyNotFound)
	_, err = GetFunctionInfoByTriggerID(ctx, "aa")
	assert.Error(t, err)
	assert.Equal(t, errmsg.TriggerNotFound, err.(snerror.SNError).Code())

	eng.EXPECT().Get(gomock.Any(), gomock.Eq("test/TriggerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//aa/")).Return(`{"FunctionName":"aa","VersionOrAlias":"bb"}`, engine.ErrTransaction)
	_, err = GetFunctionInfoByTriggerID(ctx, "aa")
	assert.Error(t, err)
	assert.Equal(t, errmsg.EtcdError, err.(snerror.SNError).Code())
}

func TestDeleteFunctionVersion(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	tran.EXPECT().Get(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return("", engine.ErrTransaction)
	err := DeleteFunctionVersion(txn, "aa", "bb")
	assert.Error(t, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return(`{"Function":{"Name":"aa"},"FunctionVersion":{"Version":"bb","Package":{"StorageType":"s3","CodeUploadType":"s3"}},"FunctionLayer":[{"Name":"layer","Version":1,"Order":1}]}`, nil)
	tran.EXPECT().Del(gomock.Eq("test/LayerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//layer/b1/aa/bb/"))
	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/")).Return(`{"RefCnt":1}`, nil)
	tran.EXPECT().Del(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/"))
	tran.EXPECT().Del(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/"))
	err = DeleteFunctionVersion(txn, "aa", "bb")
	assert.NoError(t, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return(`{"FunctionVersion":{"Package":{"StorageType":"s3","CodeUploadType":"s3"}}}`, nil)
	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/")).Return("", engine.ErrTransaction)
	err = DeleteFunctionVersion(txn, "aa", "bb")
	assert.Error(t, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return(`{"FunctionVersion":{"Package":{"StorageType":"s3","CodeUploadType":"s3"}}}`, nil)
	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/")).Return("", engine.ErrKeyNotFound)
	tran.EXPECT().Del(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/"))
	err = DeleteFunctionVersion(txn, "aa", "bb")
	assert.NoError(t, err)

	tran.EXPECT().Get(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/")).Return(`{"Function":{"Name":"aa"},"FunctionVersion":{"Package":{"StorageType":"s3","BucketID":"test","ObjectID":"test","BucketUrl":"","Token":"","CodePath":"","CodeUploadType":""},"Version":"bb"},"FunctionLayer":[{"Name":"layer","Version":1,"Order":1}]}`, nil)
	tran.EXPECT().Del(gomock.Eq("test/LayerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//layer/b1/aa/bb/"))
	tran.EXPECT().Del(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/bb/"))
	err = DeleteFunctionVersion(txn, "aa", "bb")
	assert.NoError(t, err)
}

func TestDeleteFunctionVersions(t *testing.T) {
	ctrl := gomock.NewController(t)
	defer ctrl.Finish()
	_, tran, txn := getStorage(ctrl)

	tran.EXPECT().GetPrefix(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/")).Return(
		[]string{"test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/"}, []string{`{"Function":{"Name":"aa"},"FunctionVersion":{"Version":"bb","Package":{"StorageType":"s3","CodeUploadType":"s3"}},"FunctionLayer":[{"Name":"layer","Version":1,"Order":1}]}`}, nil)
	tran.EXPECT().Del(gomock.Eq("test/LayerFunctionIndex/yrk/i1fe539427b24702acc11fbb4e134e17//layer/b1/aa/bb/"))
	tran.EXPECT().Get(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/")).Return(`{"RefCnt":1}`, nil)
	tran.EXPECT().Del(gomock.Eq("test/ObjectRefIndex/yrk/i1fe539427b24702acc11fbb4e134e17/"))
	tran.EXPECT().DelPrefix(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/"))
	err := DeleteFunctionVersions(txn, "aa")
	assert.NoError(t, err)

	tran.EXPECT().GetPrefix(gomock.Eq("test/FunctionVersion/yrk/i1fe539427b24702acc11fbb4e134e17//aa/")).Return([]string{}, []string{}, engine.ErrTransaction)
	err = DeleteFunctionVersions(txn, "aa")
	assert.Error(t, err)
}

func TestPutErr(t *testing.T) {
	ctx := fakecontext.NewContext().Context()
	dbBackup := db
	db = nil
	err := Put(ctx, "abc", "123")
	assert.Equal(t, err, errmsg.EtcdInternalError)
	db = dbBackup
}

func TestNewTxn(t *testing.T) {
	Convey("Test NewTxn with db nil err", t, func() {
		backupDb := db
		db = nil
		defer func() {
			db = backupDb
		}()
		txn := NewTxn(fakecontext.NewMockContext())
		So(txn, ShouldBeNil)
	})
}
