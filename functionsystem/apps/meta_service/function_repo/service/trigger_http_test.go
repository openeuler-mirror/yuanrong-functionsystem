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

package service

import (
	"errors"
	"testing"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/model"
	"meta_service/function_repo/storage"
)

func TestCheckResourceID(t *testing.T) {
	Convey("Test checkResourceID", t, func() {
		err := checkResourceID("")
		So(err, ShouldNotBeNil)
		err2 := checkResourceID("^&*%")
		So(err2, ShouldNotBeNil)
		err3 := checkResourceID("abc123")
		So(err3, ShouldBeNil)
	})
}

func Test_createHTTPTriggerSpec(t *testing.T) {
	Convey("Test createHTTPTriggerSpec", t, func() {
		Convey("with assert HTTPTriggerSpec err", func() {
			_, err := createHTTPTriggerSpec(&storage.Txn{}, "abc", model.FunctionQueryInfo{}, "mock-funcID", "mock-triggerID")
			So(err, ShouldNotBeNil)
		})
		Convey("with storage.CheckResourceIDExist err", func() {
			patch := gomonkey.ApplyFunc(storage.CheckResourceIDExist, func(
				txn *storage.Txn, funcName string, verOrAlias string, resourceID string) (bool, error) {
				return false, errors.New("mock err")
			})
			defer patch.Reset()
			_, err := createHTTPTriggerSpec(&storage.Txn{}, &model.HTTPTriggerSpec{}, model.FunctionQueryInfo{}, "mock-funcID", "mock-triggerID")
			So(err, ShouldNotBeNil)
		})
		Convey("with storage.CheckResourceID Exist", func() {
			patch := gomonkey.ApplyFunc(storage.CheckResourceIDExist, func(
				txn *storage.Txn, funcName string, verOrAlias string, resourceID string) (bool, error) {
				return true, nil
			})
			defer patch.Reset()
			_, err := createHTTPTriggerSpec(&storage.Txn{}, &model.HTTPTriggerSpec{}, model.FunctionQueryInfo{}, "mock-funcID", "mock-triggerID")
			So(err, ShouldNotBeNil)
		})
		Convey("with parseHTTPTriggerSpec", func() {
			patch := gomonkey.ApplyFunc(storage.CheckResourceIDExist, func(
				txn *storage.Txn, funcName string, verOrAlias string, resourceID string) (bool, error) {
				return false, nil
			}).ApplyFunc(parseHTTPTriggerSpec, func(spec *model.HTTPTriggerSpec, fid, tid string) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			_, err := createHTTPTriggerSpec(&storage.Txn{}, &model.HTTPTriggerSpec{}, model.FunctionQueryInfo{}, "mock-funcID", "mock-triggerID")
			So(err, ShouldNotBeNil)
		})
	})
}
