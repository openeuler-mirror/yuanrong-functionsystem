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
	"context"
	"errors"
	"reflect"
	"testing"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/test/fakecontext"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
)

func TestGetUncontrolledList(t *testing.T) {
	Convey("Test GetUncontrolledList with KeyNotFoundError err", t, func() {
		patches := gomonkey.ApplyMethod(reflect.TypeOf(&UncontrolledObjectPrepareStmt{}), "Execute", func(
			ps *UncontrolledObjectPrepareStmt,
		) ([]UncontrolledObjectTuple, error) {
			return nil, errmsg.KeyNotFoundError
		}).ApplyGlobalVar(&db, &generatedKV{})
		defer patches.Reset()
		_, err := GetUncontrolledList()
		So(err, ShouldBeNil)
	})
	Convey("Test GetUncontrolledList with other err", t, func() {
		patches := gomonkey.ApplyMethod(reflect.TypeOf(&UncontrolledObjectPrepareStmt{}), "Execute", func(
			ps *UncontrolledObjectPrepareStmt,
		) ([]UncontrolledObjectTuple, error) {
			return nil, errors.New("mock err")
		}).ApplyGlobalVar(&db, &generatedKV{})
		defer patches.Reset()
		_, err := GetUncontrolledList()
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetUncontrolledList", t, func() {
		patches := gomonkey.ApplyMethod(reflect.TypeOf(&UncontrolledObjectPrepareStmt{}), "Execute", func(
			ps *UncontrolledObjectPrepareStmt,
		) ([]UncontrolledObjectTuple, error) {
			return nil, nil
		}).ApplyGlobalVar(&db, &generatedKV{})
		defer patches.Reset()
		_, err := GetUncontrolledList()
		So(err, ShouldBeNil)
	})
}

func TestRemoveUncontrolled(t *testing.T) {
	Convey("Test RemoveUncontrolled TenantInfo err", t, func() {
		ctx := fakecontext.NewContext()
		err := RemoveUncontrolled(ctx, "mock-bucket", "mock-obj-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test RemoveUncontrolled UncontrolledObjectDelete err", t, func() {
		patches := gomonkey.ApplyMethod(reflect.TypeOf(&generatedKV{}), "UncontrolledObjectDelete", func(kv *generatedKV, ctx context.Context, key UncontrolledObjectKey) error {
			return errors.New("mock err")
		}).ApplyGlobalVar(&db, &generatedKV{})
		defer patches.Reset()
		ctx := fakecontext.NewMockContext()
		err := RemoveUncontrolled(ctx, "mock-bucket", "mock-obj-id")
		So(err, ShouldNotBeNil)
	})
}
