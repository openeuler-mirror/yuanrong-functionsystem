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

// Package service is processing service codes
package service

import (
	"errors"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils"
)

func TestCheckFunctionVersion(t *testing.T) {
	ctx := fakecontext.NewContext()
	ctx.InitTenantInfo(server.TenantInfo{
		BusinessID: "yrk",
		TenantID:   "qqq",
		ProductID:  "",
	})

	err := CheckFunctionVersion(ctx, "functionName")
	if err != nil {
		t.Errorf("err:%s", err.Error())
	}
}

func TestPublishFunction(t *testing.T) {
	ctx := fakecontext.NewContext()
	ctx.InitTenantInfo(server.TenantInfo{
		BusinessID: "yrk",
		TenantID:   "qqq",
		ProductID:  "",
	})
	req := model.PublishRequest{
		RevisionID:  "123456",
		VersionDesc: "version 123456",
	}

	patches := gomonkey.NewPatches()
	patches.ApplyFunc(storage.GetFunctionVersion, func(txn storage.Transaction, funcName, funcVer string) (storage.FunctionVersionValue, error) {
		if funcVer != "" && funcVer != utils.GetDefaultVersion() && funcVer != utils.GetDefaultFaaSVersion() {
			return storage.FunctionVersionValue{}, errmsg.KeyNotFoundError
		}
		return storage.FunctionVersionValue{
			Function: storage.Function{
				Version: "",
			},
			FunctionVersion: storage.FunctionVersion{
				RevisionID: "123456",
				Version:    "",
				Package: storage.Package{
					StorageType: "s3",
					S3Package: storage.S3Package{
						BucketID: "abc",
						ObjectID: "def",
					},
				},
			},
		}, nil
	})

	patches.ApplyFunc(storage.UpdateFunctionVersion, func(txn storage.Transaction, fv storage.FunctionVersionValue) error {
		return nil
	})

	defer patches.Reset()

	_, err := PublishFunction(ctx, "functionName", req)
	if err != nil {
		t.Errorf("err :%s", err.Error())
	}
}

func TestVersionNumberValidation(t *testing.T) {
	Convey("Test version number validation rule", t, func() {
		// k: version number
		// v: if ok, true if so, false is not
		cases := map[string]bool{
			// valid cases
			"B001":                  true, // upper case letters, and digits
			"B.2.3":                 true, // upper case letters, and .
			"2.3.4":                 true, // digits, and .
			"a3-b02":                true, // letters, digits, and -
			"a":                     true, // single letter
			"1":                     true, // single digits
			strings.Repeat("a", 42): true, // max length

			// invalid cases
			"a0@":                   false, // end invalid
			"???":                   false, // invalid char
			"-x":                    false, // start invalid
			"x-":                    false, // invalid end
			"000x??a":               false, // invalid char
			"-":                     false, // invalid start and end
			".":                     false, // invalid start and end
			"----------":            false, // invalid start and end
			strings.Repeat("a", 43): false, // exceed max length
		}

		for v, e := range cases {
			So((validateVersionNumber(v) == nil), ShouldEqual, e)
		}
	})

	Convey("Test version number default as expected", t, func() {
		dftV, err := validateAndSetDefaultVersionNumber("")
		So(err, ShouldBeNil)
		match, err := regexp.MatchString("^v[0-9]+-[0-9]+$", dftV)
		So(err, ShouldBeNil)
		So(match, ShouldBeTrue)
		t, err := time.Parse("v20060102-150405", dftV)
		So(err, ShouldBeNil)
		// should be now. In case ut runs slow, less than 1 min would be fine
		So(time.Now().Sub(t).Minutes(), ShouldBeLessThanOrEqualTo, 1)
	})

	Convey("Test invalid version number returns empty v and and error", t, func() {
		version, err := validateAndSetDefaultVersionNumber("-3")
		So(err, ShouldNotBeNil)
		So(version, ShouldEqual, "")
	})
}

func TestCheckRepublish(t *testing.T) {
	Convey("Test checkRepublish 1", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetFunctionVersion,
			func(txn storage.Transaction, funcName, funcVer string) (storage.FunctionVersionValue, error) {
				return storage.FunctionVersionValue{}, errmsg.KeyNotFoundError
			})
		defer patch.Reset()
		txn := &storage.Txn{}
		funcVersion := storage.FunctionVersionValue{}
		funcVersion.Function.Version = "1"
		req := model.PublishRequest{}
		err := checkRepublish(txn, funcVersion, req)
		So(err, ShouldBeNil)
	})
	Convey("Test checkRepublish 2", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetFunctionVersion,
			func(txn storage.Transaction, funcName, funcVer string) (storage.FunctionVersionValue, error) {
				return storage.FunctionVersionValue{}, errors.New("mock err")
			})
		defer patch.Reset()
		txn := &storage.Txn{}
		funcVersion := storage.FunctionVersionValue{}
		funcVersion.Function.Version = "1"
		req := model.PublishRequest{}
		err := checkRepublish(txn, funcVersion, req)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkRepublish 3", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetFunctionVersion,
			func(txn storage.Transaction, funcName, funcVer string) (storage.FunctionVersionValue, error) {
				funcVersion := storage.FunctionVersionValue{}
				funcVersion.FunctionVersion.RevisionID = "1"
				return funcVersion, nil
			})
		defer patch.Reset()
		txn := &storage.Txn{}
		funcVersion := storage.FunctionVersionValue{}
		funcVersion.Function.Version = "1"
		req := model.PublishRequest{RevisionID: "1"}
		err := checkRepublish(txn, funcVersion, req)
		So(err, ShouldBeNil)
	})
}
