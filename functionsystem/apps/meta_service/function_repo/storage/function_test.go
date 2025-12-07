/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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
	"context"
	"errors"
	"reflect"
	"testing"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/test/fakecontext"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
)

func TestGetFunctionByFunctionNameAndAlias(t *testing.T) {
	patches := gomonkey.NewPatches()
	defer patches.Reset()

	patches.ApplyFunc(GetAliasValue, func(_ server.Context, _, _ string) (AliasValue, error) {
		return AliasValue{
			FunctionVersion: "v1.0",
		}, nil
	})
	assert.Equal(t, "v1.0", GetFunctionByFunctionNameAndAlias(
		&fakecontext.Context{}, "", ""))

	patches.Reset()
	patches.ApplyFunc(GetAliasValue, func(_ server.Context, _, _ string) (AliasValue, error) {
		return AliasValue{}, errors.New("fake error! ")
	})
	assert.Equal(t, "", GetFunctionByFunctionNameAndAlias(
		&fakecontext.Context{}, "", ""))
}

func TestDeleteFunctionStatus(t *testing.T) {
	Convey("Test DeleteFunctionStatus with tenantInfo nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteFunctionStatus(txn, "mockname", "mockvar")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteFunctionStatus with delete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionStatusDelete",
			func(t *generatedTx, key FunctionStatusKey) error {
				return errmsg.MarshalError
			})
		defer patch.Reset()
		err := DeleteFunctionStatus(txn, "mockname", "mockvar")
		So(err, ShouldEqual, errmsg.MarshalError)
	})
	Convey("Test DeleteFunctionStatus", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionStatusDelete",
			func(t *generatedTx, key FunctionStatusKey) error {
				return nil
			})
		defer patch.Reset()
		err := DeleteFunctionStatus(txn, "mockname", "mockvar")
		So(err, ShouldEqual, nil)
	})
}

func TestUpdateFunctionVersion(t *testing.T) {
	functionVersionValue := FunctionVersionValue{
		Function:        Function{Name: "mockName"},
		FunctionVersion: FunctionVersion{Version: "mockVersion"},
		FunctionLayer:   nil,
	}
	Convey("Test UpdateFunctionVersion with mock Delete err", t, func() {
		patch := gomonkey.ApplyFunc(DeleteFunctionVersion, func(txn Transaction, funcName string, funcVer string) error {
			return errors.New("mock Delete err")
		})
		defer patch.Reset()
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		err := UpdateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test UpdateFunctionVersion with mock Create err", t, func() {
		patch := gomonkey.ApplyFunc(DeleteFunctionVersion, func(txn Transaction, funcName string, funcVer string) error {
			return nil
		}).ApplyFunc(CreateFunctionVersion, func(txn Transaction, fv FunctionVersionValue) error {
			return errors.New("mock create err")
		})
		defer patch.Reset()
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		err := UpdateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test UpdateFunctionVersion", t, func() {
		patch := gomonkey.ApplyFunc(DeleteFunctionVersion, func(txn Transaction, funcName string, funcVer string) error {
			return nil
		}).ApplyFunc(CreateFunctionVersion, func(txn Transaction, fv FunctionVersionValue) error {
			return nil
		})
		defer patch.Reset()
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		err := UpdateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldBeNil)
	})
}

func TestCreateFunctionVersion(t *testing.T) {
	functionVersionValue := FunctionVersionValue{
		Function:        Function{Name: "mockName"},
		FunctionVersion: FunctionVersion{Version: "mockVersion", Package: Package{CodeUploadType: "s3"}},
		FunctionLayer:   []FunctionLayer{{Name: "mockLayer", Version: 0, Order: 0}},
	}
	Convey("Test CreateFunctionVersion with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := CreateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateFunctionVersion with FunctionVersionPut err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionVersionPut",
			func(t *generatedTx, key FunctionVersionKey, val FunctionVersionValue) error {
				return errors.New("mock FunctionVersionPut err")
			})
		defer patch.Reset()
		err := CreateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateFunctionVersion with FunctionLayer err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionVersionPut",
			func(t *generatedTx, key FunctionVersionKey, val FunctionVersionValue) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerFunctionIndexPut",
			func(t *generatedTx, key LayerFunctionIndexKey, val LayerFunctionIndexValue) error {
				return errors.New("mock FunctionLayer err")
			})
		defer patch.Reset()
		err := CreateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateFunctionVersion with addObjRefCntTx err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionVersionPut",
			func(t *generatedTx, key FunctionVersionKey, val FunctionVersionValue) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerFunctionIndexPut",
			func(t *generatedTx, key LayerFunctionIndexKey, val LayerFunctionIndexValue) error {
				return nil
			}).ApplyFunc(addObjRefCntTx, func(txn Transaction, pkg Package) error {
			return errors.New("mock addObjRefCntTx err")
		})
		defer patch.Reset()
		err := CreateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateFunctionVersion with local codeUploadType", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionVersionPut",
			func(t *generatedTx, key FunctionVersionKey, val FunctionVersionValue) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerFunctionIndexPut",
			func(t *generatedTx, key LayerFunctionIndexKey, val LayerFunctionIndexValue) error {
				return nil
			})
		defer patch.Reset()
		functionVersionValue.FunctionVersion.Package.CodeUploadType = ""
		err := CreateFunctionVersion(txn, functionVersionValue)
		So(err, ShouldBeNil)
	})
}

func TestGetFunctionByFunctionNameAndVersion(t *testing.T) {
	Convey("Test GetFunctionByFunctionNameAndVersion with tenant err", t, func() {
		ctx := fakecontext.NewContext()
		_, err := GetFunctionByFunctionNameAndVersion(ctx, "mockName", "mockVersion", "")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetFunctionByFunctionNameAndVersion with FunctionVersionGet err", t, func() {
		ctx := fakecontext.NewMockContext()
		dbBackup := db
		db = &generatedKV{}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(db), "FunctionVersionGet",
			func(kv *generatedKV, ctx context.Context, key FunctionVersionKey) (FunctionVersionValue, error) {
				return FunctionVersionValue{}, errmsg.KeyNotFoundError
			})
		defer func() {
			patch.Reset()
			db = dbBackup
		}()
		_, err := GetFunctionByFunctionNameAndVersion(ctx, "mockName", "mockVersion", "")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetFunctionByFunctionNameAndVersion with FunctionVersionGet err2", t, func() {
		ctx := fakecontext.NewMockContext()
		dbBackup := db
		db = &generatedKV{}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(db), "FunctionVersionGet",
			func(kv *generatedKV, ctx context.Context, key FunctionVersionKey) (FunctionVersionValue, error) {
				return FunctionVersionValue{}, errors.New("mock err")
			})
		defer func() {
			patch.Reset()
			db = dbBackup
		}()
		_, err := GetFunctionByFunctionNameAndVersion(ctx, "mockName", "mockVersion", "")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetFunctionByFunctionNameAndVersion with FunctionStatusGet err", t, func() {
		ctx := fakecontext.NewMockContext()
		dbBackup := db
		db = &generatedKV{}
		var mockErr error
		mockErr = errmsg.KeyNotFoundError
		patch := gomonkey.ApplyMethod(reflect.TypeOf(db), "FunctionVersionGet",
			func(kv *generatedKV, ctx context.Context, key FunctionVersionKey) (FunctionVersionValue, error) {
				return FunctionVersionValue{}, nil
			}).ApplyMethod(reflect.TypeOf(db), "FunctionStatusGet",
			func(kv *generatedKV, ctx context.Context, key FunctionStatusKey) (FunctionStatusValue, error) {
				return FunctionStatusValue{}, mockErr
			})
		defer func() {
			patch.Reset()
			db = dbBackup
		}()
		_, err := GetFunctionByFunctionNameAndVersion(ctx, "mockName", "mockVersion", "")
		So(err, ShouldBeNil)
		mockErr = errors.New("mock err")
		_, err = GetFunctionByFunctionNameAndVersion(ctx, "mockName", "mockVersion", "")
		So(err, ShouldNotBeNil)
	})
}
