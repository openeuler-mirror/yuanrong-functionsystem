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
	"meta_service/function_repo/test/fakecontext"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
)

func TestAliasNameExist(t *testing.T) {
	patches := gomonkey.NewPatches()
	defer patches.Reset()

	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, errmsg.KeyNotFoundError
	})
	exist, err := AliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, false, exist)
	assert.Nil(t, err)

	patches.Reset()
	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, errors.New("fake error! ")
	})
	exist, err = AliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, false, exist)
	assert.NotNil(t, err)

	patches.Reset()
	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, nil
	})
	exist, err = AliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, true, exist)
}

func TestIsAliasNameExist(t *testing.T) {
	patches := gomonkey.NewPatches()
	defer patches.Reset()

	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, errmsg.KeyNotFoundError
	})
	exist, err := IsAliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, false, exist)
	assert.Nil(t, err)

	patches.Reset()
	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, errors.New("fake error! ")
	})
	exist, err = IsAliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, false, exist)
	assert.NotNil(t, err)

	patches.Reset()
	patches.ApplyFunc(GetAlias, func(_ *Txn, _, _ string) (AliasValue, error) {
		return AliasValue{}, nil
	})
	exist, err = IsAliasNameExist(&Txn{}, "testFunc", "testAlias")
	assert.Equal(t, true, exist)
}

func TestGetAliasValue(t *testing.T) {
	Convey("Test GetAliasValue mock db AliasGet err", t, func() {
		patch := gomonkey.ApplyMethod(reflect.TypeOf(&generatedKV{}), "AliasGet", func(_ *generatedKV, _ context.Context, _ AliasKey) (AliasValue, error) {
			return AliasValue{}, errors.New("mock db AliasGet err")
		})
		defer patch.Reset()
		ctx := fakecontext.NewMockContext()
		_, err := GetAliasValue(ctx, "mockFunc", "mockAlias")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetAliasValue", t, func() {
		patch := gomonkey.ApplyMethod(reflect.TypeOf(&generatedKV{}), "AliasGet", func(_ *generatedKV, _ context.Context, _ AliasKey) (AliasValue, error) {
			return AliasValue{}, nil
		})
		defer patch.Reset()
		ctx := fakecontext.NewMockContext()
		_, err := GetAliasValue(ctx, "mockFunc", "mockAlias")
		So(err, ShouldBeNil)
	})
	Convey("Test GetAliasValue tenant info nil err", t, func() {
		patch := gomonkey.ApplyMethod(reflect.TypeOf(&generatedKV{}), "AliasGet", func(_ *generatedKV, _ context.Context, _ AliasKey) (AliasValue, error) {
			return AliasValue{}, nil
		})
		defer patch.Reset()
		ctx := fakecontext.NewContext()
		_, err := GetAliasValue(ctx, "mockFunc", "mockAlias")
		So(err, ShouldNotBeNil)
	})
}

func TestDeleteAliasByFunctionName(t *testing.T) {
	Convey("Test DeleteAliasByFunctionName when tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteAliasByFunctionName(txn, "mockName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteAliasByFunctionName when AliasDeleteRange err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasDeleteRange",
			func(t *generatedTx, prefix AliasKey) error {
				return errors.New("mock err")
			})
		defer patch.Reset()
		err := DeleteAliasByFunctionName(txn, "mockName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteAliasByFunctionName when AliasRoutingIndexDeleteRange err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasDeleteRange",
			func(t *generatedTx, prefix AliasKey) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "AliasRoutingIndexDeleteRange",
			func(t *generatedTx, prefix AliasRoutingIndexKey) error {
				return errors.New("mock err")
			})
		defer patch.Reset()
		err := DeleteAliasByFunctionName(txn, "mockName")
		So(err, ShouldNotBeNil)
	})
}

func TestDeleteAlias2(t *testing.T) {
	Convey("Test DeleteAlias with TenantInfo nil", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewContext()}
		err := DeleteAlias(txn, "mock-func", "mock-alias", nil)
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteAlias with AliasDelete err", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewMockContext()}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasDelete", func(
			t *generatedTx, key AliasKey,
		) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteAlias(txn, "mock-func", "mock-alias", nil)
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteAlias with AliasRoutingIndexDelete err", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewMockContext()}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasDelete", func(
			t *generatedTx, key AliasKey,
		) error {
			return nil
		}).ApplyMethod(reflect.TypeOf(txn.txn), "AliasRoutingIndexDelete", func(t *generatedTx, key AliasRoutingIndexKey) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteAlias(txn, "mock-func", "mock-alias", []string{"a"})
		So(err, ShouldNotBeNil)
	})
}
