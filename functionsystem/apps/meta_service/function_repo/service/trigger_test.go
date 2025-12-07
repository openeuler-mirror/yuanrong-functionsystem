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
	"fmt"
	"reflect"
	"testing"
	"time"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"

	"meta_service/common/crypto"
	"meta_service/common/snerror"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/pkgstore"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/storage/publish"
	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils"
)

func TestMethodIsValid(t *testing.T) {
	method := "PUT"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "GET"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "POST"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "DELETE"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "get"
	if checkHTTPMethod(method) == nil {
		t.Errorf("method is invalid, method is %s", method)
	}
}

func TestHttpMethodIsValid(t *testing.T) {
	method := "PUT"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "PUT"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
	method = "PUT/GET/DELETE"
	if checkHTTPMethod(method) != nil {
		t.Errorf("method is invalid, method is %s", method)
	}
}

func TestHTTPTrigger(t *testing.T) {
	Convey("Test trigger", t, func() {
		patches := gomonkey.NewPatches()
		txn := &storage.Txn{}
		patches.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
			return txn
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error { return nil })
		patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
		patches.ApplyFunc(storage.GetTriggerInfo,
			func(txn *storage.Txn, funcName string, verOrAlias string, triggerID string) (storage.TriggerValue, error) {
				return storage.TriggerValue{}, nil
			})
		patches.ApplyFunc(storage.SaveTriggerInfo,
			func(txn *storage.Txn, funcName string, verOrAlias string, triggerID string, val storage.TriggerValue) error {
				return nil
			})
		patches.ApplyFunc(publish.AddTrigger,
			func(txn *storage.Txn, funcName, verOrAlias string, info storage.TriggerValue, trigger interface{}) error {
				return nil
			})
		defer patches.Reset()

		Convey("Test CreateTrigger", func() {
			patches.ApplyFunc(storage.GetFunctionVersion,
				func(txn storage.Transaction, funcName, funcVer string) (storage.FunctionVersionValue, error) {
					return storage.FunctionVersionValue{}, nil
				})
			patches.ApplyFunc(storage.GetTriggerByFunctionNameVersion,
				func(txn storage.Transaction, funcName string, funcVer string) ([]storage.TriggerValue, error) {
					return nil, nil
				})
			patches.ApplyFunc(storage.CheckResourceIDExist,
				func(txn *storage.Txn, funcName string, verOrAlias string, resourceID string) (bool, error) {
					return false, nil
				})
			defer patches.Reset()

			req := model.TriggerCreateRequest{
				FuncID:      "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:0-test-aa:$latest",
				TriggerType: "HTTP",
				Spec: &model.HTTPTriggerSpec{
					TriggerSpec: model.TriggerSpec{
						FuncID:      "sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:0-test-aa:$latest",
						TriggerID:   "",
						TriggerType: "HTTP",
					},
					HTTPMethod:    "PUT",
					ResourceID:    "iamsff",
					AuthFlag:      false,
					AuthAlgorithm: "",
					TriggerURL:    "",
					AppID:         "",
					AppSecret:     "",
				},
			}
			_, err := CreateTrigger(fakecontext.NewMockContext(), req)
			So(err, ShouldBeNil)
		})

		Convey("Test Delete", func() {
			patches.ApplyFunc(publish.DeleteTrigger,
				func(txn storage.Transaction, funcName, verOrAlias string, info storage.TriggerValue) error {
					return nil
				})
			defer patches.Reset()

			Convey("Test DeleteTriggerByID", func() {
				patches.ApplyFunc(storage.GetFunctionInfo,
					func(txn *storage.Txn, triggerID string) (storage.TriggerFunctionIndexValue, error) {
						return storage.TriggerFunctionIndexValue{}, nil
					})
				patches.ApplyFunc(storage.DeleteTrigger,
					func(txn *storage.Txn, funcName string, verOrAlias string, triggerID string) error {
						return nil
					})
				defer patches.Reset()

				err := DeleteTriggerByID(fakecontext.NewMockContext(), "1f05e8e5-ce0d-4c4a-903a-eae27b8f9d04")
				So(err, ShouldBeNil)
			})

			Convey("Test DeleteTriggerByFuncID", func() {
				patches.ApplyFunc(storage.GetTriggerByFunctionNameVersion,
					func(txn storage.Transaction, funcName string, funcVer string) ([]storage.TriggerValue, error) {
						return []storage.TriggerValue{
							{
								TriggerType: model.HTTPType,
								EtcdSpec:    &storage.HTTPTriggerEtcdSpec{},
							},
						}, nil
					})
				patches.ApplyFunc(storage.DeleteTriggerByFunctionNameVersion,
					func(txn storage.Transaction, funcName string, funcVer string) error {
						return nil
					})
				defer patches.Reset()

				err := DeleteTriggerByFuncID(fakecontext.NewMockContext(), "0-test-aa")
				So(err, ShouldBeNil)
			})
		})
	})
}

func TestGetHTTPTrigger(t *testing.T) {
	patches := gomonkey.NewPatches()
	patches.ApplyFunc(storage.GetFunctionInfoByTriggerID,
		func(ctx server.Context, triggerID string) (storage.TriggerFunctionIndexValue, error) {
			return storage.TriggerFunctionIndexValue{}, nil
		})
	patches.ApplyFunc(storage.GetTriggerInfoByTriggerID,
		func(ctx server.Context, funcName string, verOrAlias string, triggerID string) (storage.TriggerValue, error) {
			return storage.TriggerValue{
				TriggerType: model.HTTPType,
				EtcdSpec:    &storage.HTTPTriggerEtcdSpec{},
			}, nil
		})
	defer patches.Reset()

	_, err := GetTrigger(fakecontext.NewMockContext(), "")
	assert.NoError(t, err)
}

func TestTriggerGetList(t *testing.T) {
	patches := gomonkey.NewPatches()
	outputs := []gomonkey.OutputCell{
		{
			Values: gomonkey.Params{nil, errmsg.KeyNotFoundError},
		},
	}
	patches.ApplyFuncSeq(storage.GetTriggerInfoList, outputs)
	defer patches.Reset()

	_, err := GetTriggerList(fakecontext.NewMockContext(), 0, 0,
		"sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:function:0-test-aa:$latest")
	assert.NoError(t, err)
}

func TestUpdateTrigger(t *testing.T) {
	type args struct {
		ctx server.Context
		req model.TriggerUpdateRequest
	}
	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test update trigger success", args{fakecontext.NewMockContext(), model.TriggerUpdateRequest{
			TriggerID:   "deleteFunc",
			RevisionID:  "java1.8",
			TriggerMode: "mode",
			TriggerType: "type",
		}}, model.FunctionVersion{}, false},
		{"test update trigger success", args{fakecontext.NewMockContext(), model.TriggerUpdateRequest{
			TriggerID:   "deleteFunc",
			RevisionID:  "java1.8",
			TriggerMode: "mode",
			TriggerType: "HTTP",
			Spec: model.HTTPTriggerSpec{
				TriggerSpec: model.TriggerSpec{
					FuncID:      "sn:cn:businessid:tenantid@productid:function:test:$latest",
					TriggerID:   "",
					TriggerType: "HTTP",
				},
				HTTPMethod:    "PUT",
				ResourceID:    "iamsff",
				AuthFlag:      false,
				AuthAlgorithm: "",
				TriggerURL:    "",
				AppID:         "",
				AppSecret:     "",
			},
		}}, model.FunctionVersion{}, false},
	}

	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		_, err := UpdateTrigger(tt.args.ctx, tt.args.req)
		assert.NotNil(t, err)
		assert.Equal(t, errmsg.InvalidUserParam, err.(snerror.SNError).Code())
	})
	tt = tests[1]
	t.Run(tt.name, func(t *testing.T) {
		_, err := UpdateTrigger(tt.args.ctx, tt.args.req)
		assert.NotNil(t, err)
		assert.Equal(t, errmsg.InvalidUserParam, err.(snerror.SNError).Code())
	})
}

func TestCheckHTTPTriggerAppInfo(t *testing.T) {
	Convey("Test checkHTTPTriggerAppInfo", t, func() {
		spec := &model.HTTPTriggerSpec{AuthAlgorithm: "mockAlgo"}
		err := checkHTTPTriggerAppInfo(spec)
		So(err, ShouldBeNil)
	})
	Convey("Test checkHTTPTriggerAppInfo invalid appID len err", t, func() {
		spec := &model.HTTPTriggerSpec{AuthAlgorithm: defaultAlgorithm}
		err := checkHTTPTriggerAppInfo(spec)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkHTTPTriggerAppInfo encrypt invalid appSecret err", t, func() {
		spec := &model.HTTPTriggerSpec{AuthAlgorithm: defaultAlgorithm, AppID: "123"}
		err := checkHTTPTriggerAppInfo(spec)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkHTTPTriggerAppInfo failed to encrypt appSecret err", t, func() {
		spec := &model.HTTPTriggerSpec{AuthAlgorithm: defaultAlgorithm, AppID: "123", AppSecret: "123"}
		patch := gomonkey.ApplyFunc(crypto.Encrypt, func(content string, secret []byte) ([]byte, error) {
			return nil, errors.New("mock err")
		})
		defer patch.Reset()
		err := checkHTTPTriggerAppInfo(spec)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkHTTPTriggerAppInfo", t, func() {
		spec := &model.HTTPTriggerSpec{AuthAlgorithm: defaultAlgorithm, AppID: "123", AppSecret: "123"}
		patch := gomonkey.ApplyFunc(crypto.Encrypt, func(content string, secret []byte) ([]byte, error) {
			return []byte{0}, nil
		})
		defer patch.Reset()
		err := checkHTTPTriggerAppInfo(spec)
		So(err, ShouldBeNil)
	})

}

func TestStoreUpdateTrigger(t *testing.T) {
	ctx := fakecontext.NewMockContext()
	txn := storage.NewTxn(ctx)
	info := storage.TriggerValue{
		TriggerID:   "abc123",
		FuncName:    "mock-func",
		TriggerType: "",
		RevisionID:  "1",
		RawEtcdSpec: nil,
		EtcdSpec:    nil,
		CreateTime:  time.Time{},
		UpdateTime:  time.Time{},
	}
	funcInfo := storage.TriggerFunctionIndexValue{
		FunctionName:   "mock-func",
		VersionOrAlias: "1",
	}
	Convey("Test storeUpdateTrigger with GetTriggerInfo err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{}, errmsg.KeyNotFoundError
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with KeyNotFoundError", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{}, errors.New("mock err")
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with revisionId not same", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "2"}, nil
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with convert request err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "1"}, nil
			}).ApplyFunc(buildModelTriggerSpec,
			func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
				return nil, errors.New("mock err")
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with empty version and alias err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "1"}, nil
			}).ApplyFunc(buildModelTriggerSpec,
			func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
				return nil, nil
			})
		defer patch.Reset()
		funcInfo2 := storage.TriggerFunctionIndexValue{
			FunctionName:   "mock-func",
			VersionOrAlias: "",
		}
		err := storeUpdateTrigger(txn, info, funcInfo2, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with SaveTriggerInfo err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "1"}, nil
			}).ApplyFunc(buildModelTriggerSpec,
			func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
				return nil, nil
			}).ApplyFunc(storage.SaveTriggerInfo,
			func(*storage.Txn, string, string, string, storage.TriggerValue) error {
				return errors.New("mock err")
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger with publish trigger err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "1"}, nil
			}).ApplyFunc(buildModelTriggerSpec,
			func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
				return nil, nil
			}).ApplyFunc(storage.SaveTriggerInfo,
			func(*storage.Txn, string, string, string, storage.TriggerValue) error {
				return nil
			}).ApplyFunc(publish.AddTrigger,
			func(*storage.Txn, string, string, storage.TriggerValue, interface{}) error {
				return errors.New("mock err")
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldNotBeNil)
	})
	Convey("Test storeUpdateTrigger", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo,
			func(*storage.Txn, string, string, string) (storage.TriggerValue, error) {
				return storage.TriggerValue{RevisionID: "1"}, nil
			}).ApplyFunc(buildModelTriggerSpec,
			func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
				return nil, nil
			}).ApplyFunc(storage.SaveTriggerInfo,
			func(*storage.Txn, string, string, string, storage.TriggerValue) error {
				return nil
			}).ApplyFunc(publish.AddTrigger,
			func(*storage.Txn, string, string, storage.TriggerValue, interface{}) error {
				return nil
			})
		defer patch.Reset()
		err := storeUpdateTrigger(txn, info, funcInfo, "mock-func-id")
		So(err, ShouldBeNil)
	})
}

func TestCheckFuncInfo(t *testing.T) {
	txn := &storage.Txn{}
	info := model.FunctionQueryInfo{AliasName: "abc", FunctionVersion: "abcd"}
	Convey("Test checkFuncInfo", t, func() {
		patch := gomonkey.ApplyFunc(checkFunctionNameAndVersion, func(*storage.Txn, string, string) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := checkFuncInfo(txn, info)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkFuncInfo 2", t, func() {
		patch := gomonkey.ApplyFunc(checkFunctionNameAndVersion, func(*storage.Txn, string, string) error {
			return nil
		}).ApplyFunc(storage.AliasNameExist, func(txn *storage.Txn, funcName, aliasName string) (bool, error) {
			return false, errors.New("mock err")
		})
		defer patch.Reset()
		err := checkFuncInfo(txn, info)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkFuncInfo 3", t, func() {
		patch := gomonkey.ApplyFunc(checkFunctionNameAndVersion, func(*storage.Txn, string, string) error {
			return nil
		}).ApplyFunc(storage.AliasNameExist, func(txn *storage.Txn, funcName, aliasName string) (bool, error) {
			return false, nil
		})
		defer patch.Reset()
		err := checkFuncInfo(txn, info)
		So(err, ShouldNotBeNil)
	})
	Convey("Test checkFuncInfo 4", t, func() {
		patch := gomonkey.ApplyFunc(checkFunctionNameAndVersion, func(*storage.Txn, string, string) error {
			return nil
		}).ApplyFunc(storage.AliasNameExist, func(txn *storage.Txn, funcName, aliasName string) (bool, error) {
			return true, nil
		})
		defer patch.Reset()
		err := checkFuncInfo(txn, info)
		So(err, ShouldBeNil)
	})
	Convey("Test checkFuncInfo 5", t, func() {
		info.AliasName = ""
		patch := gomonkey.ApplyFunc(checkFunctionNameAndVersion, func(tx *storage.Txn, name string, ver string) error {
			if ver == "abcd" {
				return errors.New("mock err")
			}
			return nil
		})
		defer patch.Reset()
		err := checkFuncInfo(txn, info)
		So(err, ShouldNotBeNil)
	})
}

func UpdateTriggerPatches(setErr int) [6]*gomonkey.Patches {
	patches := [...]*gomonkey.Patches{
		gomonkey.ApplyFunc(updateTrigger,
			func(req model.TriggerUpdateRequest) (storage.TriggerFunctionIndexValue, storage.TriggerValue, error) {
				return storage.TriggerFunctionIndexValue{}, storage.TriggerValue{}, nil
			}),
		gomonkey.ApplyMethod(reflect.TypeOf(&fakecontext.Context{}), "TenantInfo",
			func(c *fakecontext.Context) (server.TenantInfo, error) {
				if setErr == 1 {
					return server.TenantInfo{}, errors.New("mock err")
				}
				return server.TenantInfo{}, nil
			}),
		gomonkey.ApplyFunc(storeUpdateTrigger, func(
			txn *storage.Txn, info storage.TriggerValue, funcInfo storage.TriggerFunctionIndexValue, funcID string,
		) error {
			if setErr == 2 {
				return errors.New("mock err")
			}
			return nil
		}),
		gomonkey.ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Commit", func(tx *storage.Txn) error {
			if setErr == 3 {
				return errors.New("mock err")
			}
			return nil
		}),
		gomonkey.ApplyFunc(buildTriggerResponse, func(
			ctx server.Context, triggerInfo storage.TriggerValue, funcName, verOrAlias string,
		) (model.TriggerInfo, error) {
			if setErr == 4 {
				return model.TriggerInfo{}, errors.New("mock err")
			}
			return model.TriggerInfo{}, nil
		}),
		gomonkey.ApplyFunc(utils.BuildTriggerURN, func(info server.TenantInfo, funcName, verOrAlias string) string {
			return "abc123"
		}),
	}
	return patches
}

func TestUpdateTrigger2(t *testing.T) {
	ctx := fakecontext.NewMockContext()
	txnPatch := gomonkey.ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Cancel", func(tx *storage.Txn) {
		return
	})
	defer txnPatch.Reset()
	Convey("Test UpdateTrigger", t, func() {
		for i := 0; i <= 4; i++ {
			fmt.Println("case ", i)
			patches := UpdateTriggerPatches(i)
			req := model.TriggerUpdateRequest{}
			_, err := UpdateTrigger(ctx, req)
			if i == 0 {
				So(err, ShouldBeNil)
			} else {
				So(err, ShouldNotBeNil)
			}
			for _, patch := range patches {
				patch.Reset()
			}
		}
	})
}

func TestCheckFunctionNameAndVersion(t *testing.T) {
	Convey("Test checkFunctionNameAndVersion", t, func() {
		txn := &storage.Txn{}
		Convey("when KeyNotFoundError", func() {
			patch := gomonkey.ApplyFunc(storage.GetFunctionVersion,
				func(storage.Transaction, string, string) (storage.FunctionVersionValue, error) {
					return storage.FunctionVersionValue{}, errmsg.KeyNotFoundError
				})
			defer patch.Reset()
			err := checkFunctionNameAndVersion(txn, "abc", "1")
			So(err, ShouldNotBeNil)
		})
		Convey("when other err", func() {
			patch := gomonkey.ApplyFunc(storage.GetFunctionVersion,
				func(storage.Transaction, string, string) (storage.FunctionVersionValue, error) {
					return storage.FunctionVersionValue{}, errors.New("mock err")
				})
			defer patch.Reset()
			err := checkFunctionNameAndVersion(txn, "abc", "1")
			So(err, ShouldNotBeNil)
		})

	})

}

func TestGetTriggerList(t *testing.T) {
	ctx := fakecontext.NewMockContext()
	Convey("Test GetTriggerList", t, func() {
		Convey("when ParseFunctionInfo err", func() {
			patches := gomonkey.ApplyFunc(ParseFunctionInfo,
				func(ctx server.Context, queryInfo, qualifier string) (model.FunctionQueryInfo, error) {
					return model.FunctionQueryInfo{}, errors.New("mock err")
				})
			defer patches.Reset()
			_, err := GetTriggerList(ctx, 0, 0, "mock-fid")
			So(err, ShouldNotBeNil)
		})
		patch := gomonkey.ApplyFunc(ParseFunctionInfo,
			func(ctx server.Context, queryInfo, qualifier string) (model.FunctionQueryInfo, error) {
				return model.FunctionQueryInfo{}, nil
			})
		defer patch.Reset()
		Convey("when GetTriggerInfoList err", func() {
			patch2 := gomonkey.ApplyFunc(storage.GetTriggerInfoList,
				func(server.Context, string, string, int, int) ([]storage.TriggerValue, error) {
					return nil, errors.New("mock err")
				})
			defer patch2.Reset()
			_, err := GetTriggerList(ctx, 0, 0, "mock-fid")
			So(err, ShouldNotBeNil)
		})
		Convey("when buildTriggerResponse err", func() {
			patch2 := gomonkey.ApplyFunc(storage.GetTriggerInfoList,
				func(server.Context, string, string, int, int) ([]storage.TriggerValue, error) {
					triggerValues := []storage.TriggerValue{{}}
					return triggerValues, nil
				}).ApplyFunc(buildTriggerResponse,
				func(server.Context, storage.TriggerValue, string, string) (model.TriggerInfo, error) {
					return model.TriggerInfo{}, errors.New("mock err")
				})
			defer patch2.Reset()
			_, err := GetTriggerList(ctx, 0, 0, "mock-fid")
			So(err, ShouldNotBeNil)
		})
		Convey("when ok", func() {
			patch2 := gomonkey.ApplyFunc(storage.GetTriggerInfoList,
				func(server.Context, string, string, int, int) ([]storage.TriggerValue, error) {
					triggerValues := []storage.TriggerValue{{}}
					return triggerValues, nil
				}).ApplyFunc(buildTriggerResponse,
				func(server.Context, storage.TriggerValue, string, string) (model.TriggerInfo, error) {
					return model.TriggerInfo{}, nil
				})
			defer patch2.Reset()
			_, err := GetTriggerList(ctx, 0, 0, "mock-fid")
			So(err, ShouldBeNil)
		})
	})
}

func TestDeleteTriggerByID(t *testing.T) {
	ctx := fakecontext.NewMockContext()
	txn := &storage.Txn{}
	patches := gomonkey.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
		return txn
	})
	patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
	defer patches.Reset()
	Convey("Test DeleteTriggerByID with GetFunctionInfo err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetFunctionInfo, func(
			*storage.Txn, string) (storage.TriggerFunctionIndexValue, error) {
			return storage.TriggerFunctionIndexValue{}, errmsg.KeyNotFoundError
		})
		err := DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
		patch.Reset()

		patch.ApplyFunc(storage.GetFunctionInfo, func(
			*storage.Txn, string) (storage.TriggerFunctionIndexValue, error) {
			return storage.TriggerFunctionIndexValue{}, errors.New("mock err")
		})
		err = DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
		patch.Reset()
	})
	patches.ApplyFunc(storage.GetFunctionInfo, func(
		*storage.Txn, string) (storage.TriggerFunctionIndexValue, error) {
		return storage.TriggerFunctionIndexValue{}, nil
	})
	Convey("Test DeleteTriggerByID with GetTriggerInfo err", t, func() {
		patch := gomonkey.ApplyFunc(storage.GetTriggerInfo, func(
			*storage.Txn, string, string, string) (storage.TriggerValue, error) {
			return storage.TriggerValue{}, errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
	})
	patches.ApplyFunc(storage.GetTriggerInfo, func(
		*storage.Txn, string, string, string) (storage.TriggerValue, error) {
		return storage.TriggerValue{}, nil
	})
	Convey("Test DeleteTriggerByID with storage.DeleteTrigger err", t, func() {
		patch := gomonkey.ApplyFunc(storage.DeleteTrigger, func(*storage.Txn, string, string, string) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
	})
	patches.ApplyFunc(storage.DeleteTrigger, func(*storage.Txn, string, string, string) error {
		return nil
	})
	Convey("Test DeleteTriggerByID with publish.DeleteTrigger err", t, func() {
		patch := gomonkey.ApplyFunc(publish.DeleteTrigger, func(
			txn storage.Transaction, funcName, verOrAlias string, info storage.TriggerValue) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
	})
	patches.ApplyFunc(publish.DeleteTrigger, func(
		txn storage.Transaction, funcName, verOrAlias string, info storage.TriggerValue) error {
		return nil
	})
	Convey("Test DeleteTriggerByID with txn.Commit err", t, func() {
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := DeleteTriggerByID(ctx, "mock-tid")
		So(err, ShouldNotBeNil)
	})
}

func Test_updateTrigger2(t *testing.T) {
	Convey("Test updateTrigger 2", t, func() {
		req := model.TriggerUpdateRequest{
			TriggerType: "mock-trigger",
		}
		trigger := triggerSpecTable[model.HTTPType]
		trigger.Update = func(raw interface{}) (storage.TriggerSpec, string, error) {
			return nil, "", nil
		}
		triggerSpecTable[req.TriggerType] = trigger
		Convey("with CheckAndGetVerOrAlias err", func() {
			patch := gomonkey.ApplyFunc(CheckAndGetVerOrAlias, func(
				funcID, qualifier string) (model.FunctionQueryInfo, error) {
				return model.FunctionQueryInfo{}, errors.New("mock err")
			})
			defer patch.Reset()
			_, _, err := updateTrigger(req)
			So(err, ShouldNotBeNil)
		})
		Convey("with success", func() {
			patch := gomonkey.ApplyFunc(CheckAndGetVerOrAlias, func(
				funcID, qualifier string) (model.FunctionQueryInfo, error) {
				return model.FunctionQueryInfo{}, nil
			})
			defer patch.Reset()
			_, _, err := updateTrigger(req)
			So(err, ShouldBeNil)
		})
		Convey("with TriggerType not contain in triggerSpecTable", func() {
			req.TriggerType = "mock-trigger2"
			_, _, err := updateTrigger(req)
			So(err, ShouldNotBeNil)
		})
	})
}

func Test_storeCreateTrigger(t *testing.T) {
	Convey("Test storeCreateTrigger", t, func() {
		mode := 0
		patches := gomonkey.ApplyFunc(buildModelTriggerSpec, func(info storage.TriggerValue, funcID string) (spec interface{}, err error) {
			if mode == 1 {
				return nil, errors.New("mock err")
			}
			return nil, nil
		}).ApplyFunc(getVerOrAlias, func(i model.FunctionQueryInfo) string {
			if mode == 2 {
				return ""
			}
			return "mock-ver-or-alias"
		}).ApplyFunc(storage.SaveTriggerInfo, func(txn *storage.Txn, funcName string,
			verOrAlias string, triggerID string, val storage.TriggerValue) error {
			if mode == 3 {
				return errors.New("mock err")
			}
			return nil
		}).ApplyFunc(publish.AddTrigger, func(txn *storage.Txn, funcName, verOrAlias string,
			info storage.TriggerValue, value interface{}) error {
			if mode == 4 {
				return errors.New("mock err")
			}
			return nil
		})
		defer patches.Reset()
		for mode = 0; mode < 5; mode++ {
			err := storeCreateTrigger(&storage.Txn{}, storage.TriggerValue{}, model.FunctionQueryInfo{}, "mock-fun-id")
			if mode == 0 {
				So(err, ShouldBeNil)
			} else {
				So(err, ShouldNotBeNil)
			}
		}
	})
}

func TestDeleteLayerVersion(t *testing.T) {
	Convey("Test DeleteLayerVersion", t, func() {
		mode := 0
		patches := gomonkey.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
			return &storage.Txn{}
		}).ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Cancel", func(_ *storage.Txn) {
			return
		}).ApplyFunc(storage.GetLayerVersionTx, func(txn storage.Transaction, layerName string, layerVersion int) (storage.LayerValue, error) {
			if mode == 1 {
				return storage.LayerValue{}, errmsg.KeyNotFoundError
			} else if mode == 2 {
				return storage.LayerValue{}, errors.New("mock err")
			}
			return storage.LayerValue{}, nil
		}).ApplyFunc(storage.IsLayerVersionUsed, func(txn *storage.Txn, layerName string, layerVersion int) (bool, error) {
			if mode == 3 {
				return true, errors.New("mock err")
			} else if mode == 4 {
				return true, nil
			}
			return false, nil
		}).ApplyFunc(storage.AddUncontrolledTx, func(txn storage.Transaction, bucketID, objectID string) error {
			if mode == 5 {
				return errors.New("mock err")
			}
			return nil
		}).ApplyFunc(storage.DeleteLayerVersion, func(txn *storage.Txn, layerName string, layerVersion int) error {
			if mode == 6 {
				return errors.New("mock err")
			}
			return nil
		}).ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Commit", func(_ *storage.Txn) error {
			if mode == 7 {
				return errors.New("mock err")
			}
			return nil
		}).ApplyFunc(pkgstore.Delete, func(bucketID, objectID string) error {
			return nil
		}).ApplyFunc(storage.RemoveUncontrolled, func(ctx server.Context, bucketID, objectID string) error {
			return nil
		})
		defer patches.Reset()
		for mode = 0; mode < 8; mode++ {
			err := DeleteLayerVersion(fakecontext.NewMockContext(), "mock-layer-name", 1)
			if mode == 0 {
				So(err, ShouldBeNil)
			} else {
				So(err, ShouldNotBeNil)
			}
		}
	})
}

func TestCreateTrigger(t *testing.T) {
	Convey("Test CreateTrigger", t, func() {
		mode := 0
		patches := gomonkey.NewPatches()
		txn := &storage.Txn{}
		patches.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
			return txn
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
		defer patches.Reset()
		patches.ApplyFunc(ParseFunctionInfo, func(ctx server.Context, queryInfo, qualifier string) (model.FunctionQueryInfo, error) {
			if mode == 0 {
				return model.FunctionQueryInfo{}, errors.New("mock err")
			}
			return model.FunctionQueryInfo{}, nil
		})
		patches.ApplyFunc(checkFuncInfo, func(txn *storage.Txn, info model.FunctionQueryInfo) error {
			if mode == 1 {
				return errors.New("mock err")
			}
			return nil
		})
		patches.ApplyFunc(storage.GetTriggerByFunctionNameVersion, func(
			txn storage.Transaction, funcName string, funcVer string) ([]storage.TriggerValue, error) {
			if mode == 2 {
				return nil, errors.New("mock err")
			} else if mode == 3 {
				return make([]storage.TriggerValue, 34, 34), nil
			}
			return make([]storage.TriggerValue, 1, 1), nil
		})
		patches.ApplyFunc(createTrigger, func(txn *storage.Txn, req model.TriggerCreateRequest, funcInfo model.FunctionQueryInfo,
		) (storage.TriggerValue, error) {
			if mode == 4 {
				return storage.TriggerValue{}, errors.New("mock err")
			}
			return storage.TriggerValue{}, nil
		})
		patches.ApplyFunc(storeCreateTrigger, func(
			txn *storage.Txn, triggerInfo storage.TriggerValue, funcInfo model.FunctionQueryInfo, funcID string,
		) error {
			if mode == 5 {
				return errors.New("mock err")
			}
			return nil
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
			if mode == 6 {
				return errors.New("mock err")
			}
			return nil
		})
		patches.ApplyFunc(buildTriggerResponse, func(ctx server.Context, triggerInfo storage.TriggerValue, funcName, verOrAlias string,
		) (model.TriggerInfo, error) {
			if mode == 7 {
				return model.TriggerInfo{}, errors.New("mock err")
			}
			return model.TriggerInfo{}, nil
		})
		for mode = 0; mode < 8; mode++ {
			_, err := CreateTrigger(fakecontext.NewMockContext(), model.TriggerCreateRequest{})
			So(err, ShouldNotBeNil)
		}
	})
}
