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

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"

	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/storage/publish"
	"meta_service/function_repo/test/fakecontext"
)

func TestCreateAlias(t *testing.T) {
	type args struct {
		ctx server.Context
		req model.AliasRequest
	}
	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test create alias", args{fakecontext.NewMockContext(),
			model.AliasRequest{
				Name:            "test",
				FunctionVersion: "",
				Description:     "des",
				RoutingConfig:   nil,
			}},
			model.FunctionVersion{}, false},
	}
	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		_, err := CreateAlias(tt.args.ctx, "name", tt.args.req)
		assert.NotEqual(t, err, nil)
	})
}

func createAliasPatches(setErr int) [10]*gomonkey.Patches {
	patches := [...]*gomonkey.Patches{
		gomonkey.ApplyFunc(CheckFuncVersion, func(txn *storage.Txn, fName, fVersion string) error {
			if setErr == 0 {
				return errors.New("mock err CheckFuncVersion")
			}
			return nil
		}),
		gomonkey.ApplyFunc(CheckAliasName, func(txn *storage.Txn, fName, aliasName string) error {
			if setErr == 1 {
				return errors.New("mock err CheckAliasName")
			}
			return nil
		}),
		gomonkey.ApplyFunc(CheckFunctionAliasNum, func(txn *storage.Txn, funcName string) error {
			if setErr == 2 {
				return errors.New("mock err CheckFunctionAliasNum")
			}
			return nil
		}),
		gomonkey.ApplyFunc(CheckRoutingConfig, func(*storage.Txn, string, map[string]int) error {
			if setErr == 3 {
				return errors.New("mock err CheckRoutingConfig")
			}
			return nil
		}),
		gomonkey.ApplyFunc(storage.CreateAlias, func(txn *storage.Txn, alias storage.AliasValue) error {
			if setErr == 4 {
				return errors.New("mock err storage.CreateAlias")
			}
			return nil
		}),
		gomonkey.ApplyFunc(buildAliasEtcd, func(server.Context, storage.AliasValue) (publish.AliasEtcd, error) {
			if setErr == 5 {
				return publish.AliasEtcd{}, errors.New("mock err buildAliasEtcd")
			}
			return publish.AliasEtcd{}, nil
		}),
		gomonkey.ApplyFunc(publish.CreateAliasEtcd, func(*storage.Txn, string, publish.AliasEtcd) error {
			if setErr == 6 {
				return errors.New("mock err publish.CreateAliasEtcd")
			}
			return nil
		}),
		gomonkey.ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Commit", func(tx *storage.Txn) error {
			if setErr == 7 {
				return errors.New("mock err Commit")
			}
			return nil
		}),
		gomonkey.ApplyFunc(buildAliasResponse, func(server.Context, storage.AliasValue) (model.AliasResponse, error) {
			if setErr == 8 {
				return model.AliasResponse{}, errors.New("mock err buildAliasResponse")
			}
			return model.AliasResponse{}, nil
		}),
		gomonkey.ApplyMethod(reflect.TypeOf(&storage.Txn{}), "Cancel", func(tx *storage.Txn) {
			return
		}),
	}
	return patches
}

func TestCreateAlias2(t *testing.T) {
	req := model.AliasRequest{
		Name:            "test",
		FunctionVersion: "",
		Description:     "des",
		RoutingConfig:   nil,
	}
	Convey("Test CreateAlias 2", t, func() {
		ctx := fakecontext.NewMockContext()
		for i := 0; i <= 9; i++ {
			fmt.Println("case ", i)
			patches := createAliasPatches(i)
			_, err := CreateAlias(ctx, "mock-name", req)
			if i == 9 {
				So(err, ShouldBeNil)
			} else {
				So(err, ShouldNotBeNil)
			}
			for j, _ := range patches {
				patches[j].Reset()
			}
		}

	})
}

func TestUpdateAlias(t *testing.T) {
	type args struct {
		ctx server.Context
		req model.AliasUpdateRequest
	}
	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test update alias", args{fakecontext.NewMockContext(),
			model.AliasUpdateRequest{
				FunctionVersion: "",
				Description:     "des",
				RevisionID:      "rev",
				RoutingConfig:   nil,
			}},
			model.FunctionVersion{}, false},
	}
	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		_, err := UpdateAlias(tt.args.ctx, "name", "alias", tt.args.req)
		assert.NotEqual(t, err, nil)
	})
}

func TestUpdateAlias2(t *testing.T) {
	patch := gomonkey.ApplyFunc(storage.GetAlias, func(txn *storage.Txn, funcName, aliasName string) (
		storage.AliasValue, error) {
		return storage.AliasValue{}, errmsg.KeyNotFoundError
	})
	defer patch.Reset()

	req := model.AliasUpdateRequest{
		FunctionVersion: "",
		Description:     "des",
		RevisionID:      "rev",
		RoutingConfig:   nil,
	}
	ctx := fakecontext.NewMockContext()
	_, err := UpdateAlias(ctx, "name", "alias", req)
	assert.NotEqual(t, errmsg.EtcdInternalError, err)

	patch = gomonkey.ApplyFunc(storage.GetAlias, func(txn *storage.Txn, funcName, aliasName string) (
		storage.AliasValue, error) {
		return storage.AliasValue{}, errmsg.EtcdInternalError
	})
	defer patch.Reset()
	_, err = UpdateAlias(ctx, "name", "alias", req)
	assert.Equal(t, errmsg.EtcdInternalError, err)

	patch = gomonkey.ApplyFunc(storage.GetAlias, func(txn *storage.Txn, funcName, aliasName string) (
		storage.AliasValue, error) {
		return storage.AliasValue{RevisionID: "rev2"}, nil
	})
	defer patch.Reset()
	_, err = UpdateAlias(ctx, "name", "alias", req)
	assert.NotNil(t, err)

	patch = gomonkey.ApplyFunc(storage.GetAlias, func(txn *storage.Txn, funcName, aliasName string) (
		storage.AliasValue, error) {
		return storage.AliasValue{RevisionID: "rev"}, nil
	})
	defer patch.Reset()
	patch2 := gomonkey.ApplyFunc(storage.IsFuncVersionExist, func(txn *storage.Txn, funcName, funcVersion string) (bool, error) {
		return false, errmsg.EtcdInternalError
	})
	defer patch2.Reset()
	_, err = UpdateAlias(ctx, "name", "alias", req)
	assert.Equal(t, errmsg.EtcdInternalError, err)

	patch2 = gomonkey.ApplyFunc(storage.IsFuncVersionExist, func(txn *storage.Txn, funcName, funcVersion string) (bool, error) {
		return false, nil
	})
	defer patch2.Reset()
	_, err = UpdateAlias(ctx, "name", "alias", req)
	assert.NotNil(t, err)

	req = model.AliasUpdateRequest{
		FunctionVersion: "",
		Description:     "des",
		RevisionID:      "rev",
		RoutingConfig:   map[string]int{"1": 50, "2": 50},
	}
	patch2 = gomonkey.ApplyFunc(storage.IsFuncVersionExist, func(txn *storage.Txn, funcName, funcVersion string) (bool, error) {
		return true, nil
	})
	defer patch2.Reset()
	patch3 := gomonkey.ApplyFunc(storage.CreateAlias, func(txn *storage.Txn, alias storage.AliasValue) error {
		return nil
	})
	defer patch3.Reset()
	patch4 := gomonkey.ApplyFunc(publish.CreateAliasEtcd, func(txn *storage.Txn, funcName string, aliasEtcd publish.AliasEtcd) error {
		return errmsg.EtcdInternalError
	})
	defer patch4.Reset()
	_, err = UpdateAlias(ctx, "name", "alias", req)
	assert.Equal(t, errmsg.EtcdInternalError, err)
}

func TestDeleteAlias(t *testing.T) {
	type args struct {
		ctx server.Context
		req model.AliasDeleteRequest
	}
	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test delete alias", args{fakecontext.NewMockContext(),
			model.AliasDeleteRequest{
				FunctionName: "",
				AliasName:    "des",
			}},
			model.FunctionVersion{}, false},
	}
	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		err := DeleteAlias(tt.args.ctx, tt.args.req)
		assert.NotEqual(t, err, nil)
	})
}

func TestGetAlias(t *testing.T) {
	type args struct {
		ctx server.Context
		req model.AliasQueryRequest
	}
	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test delete alias", args{fakecontext.NewMockContext(),
			model.AliasQueryRequest{
				FunctionName: "",
				AliasName:    "des",
			}},
			model.FunctionVersion{}, false},
	}
	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		_, err := GetAlias(tt.args.ctx, tt.args.req)
		assert.NotEqual(t, err, nil)
	})
}

func TestBuildAlias(t *testing.T) {
	Convey("Test buildAlias", t, func() {
		aliasValue := buildAlias("fakeName", model.AliasRequest{
			Name:            "",
			FunctionVersion: "",
			Description:     "abc",
			RoutingConfig:   nil,
		})
		So(aliasValue.Description, ShouldEqual, "abc")
	})
}

func TestBuildAliasResponse(t *testing.T) {
	Convey("Test buildAliasResponse with ctx nil", t, func() {
		ctx := fakecontext.NewContext()
		_, err := buildAliasResponse(ctx, storage.AliasValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test buildAliasResponse", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyGlobalVar(&config.RepoCfg, new(config.Configs))
		defer patch.Reset()
		config.RepoCfg.URNCfg.Prefix = "p"
		config.RepoCfg.URNCfg.Zone = "z"
		_, err := buildAliasResponse(ctx, storage.AliasValue{})
		So(err, ShouldBeNil)
	})
}

func TestCheckAliasName(t *testing.T) {
	Convey("Test CheckAliasName with IsAliasNameExist err", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyFunc(storage.IsAliasNameExist, func(txn *storage.Txn, fName, aName string) (bool, error) {
			return false, errors.New("mock err")
		})
		defer patch.Reset()
		err := CheckAliasName(&txn, "mockFName", "mockAliasName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test CheckAliasName with IsExist", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyFunc(storage.IsAliasNameExist, func(txn *storage.Txn, fName, aName string) (bool, error) {
			return true, nil
		})
		defer patch.Reset()
		err := CheckAliasName(&txn, "mockFName", "mockAliasName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test CheckAliasName with not IsExist", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyFunc(storage.IsAliasNameExist, func(txn *storage.Txn, fName, aName string) (bool, error) {
			return false, nil
		})
		defer patch.Reset()
		err := CheckAliasName(&txn, "mockFName", "mockAliasName")
		So(err, ShouldBeNil)
	})
}

func TestCheckFunctionAliasNum(t *testing.T) {
	Convey("Test CheckFunctionAliasNum with GetAliasNumByFunctionName err", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyGlobalVar(&config.RepoCfg, new(config.Configs))
		patch = patch.ApplyFunc(storage.GetAliasNumByFunctionName, func(txn storage.Transaction, funcName string) (int, error) {
			return 0, errors.New("mock err")
		})
		defer patch.Reset()
		err := CheckFunctionAliasNum(&txn, "mockName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test CheckFunctionAliasNum with number > AliasMax", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyGlobalVar(&config.RepoCfg, new(config.Configs))
		patch = patch.ApplyFunc(storage.GetAliasNumByFunctionName, func(txn storage.Transaction, funcName string) (int, error) {
			return 1, nil
		})
		defer patch.Reset()
		err := CheckFunctionAliasNum(&txn, "mockName")
		So(err, ShouldNotBeNil)
	})
	Convey("Test CheckFunctionAliasNum", t, func() {
		txn := storage.Txn{}
		patch := gomonkey.ApplyGlobalVar(&config.RepoCfg, new(config.Configs))
		config.RepoCfg.FunctionCfg.AliasMax = 1
		patch = patch.ApplyFunc(storage.GetAliasNumByFunctionName, func(txn storage.Transaction, funcName string) (int, error) {
			return 0, nil
		})
		defer patch.Reset()
		err := CheckFunctionAliasNum(&txn, "mockName")
		So(err, ShouldBeNil)
	})
}

func TestGetAliaseList(t *testing.T) {
	Convey("Test GetAliaseList with GetAliasesByPage err", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyFunc(storage.GetAliasesByPage,
			func(ctx server.Context, funcName string, pageIndex, pageSize int) ([]storage.AliasValue, error) {
				return nil, errors.New("mock err")
			})
		defer patch.Reset()
		_, err := GetAliaseList(ctx, model.AliasListQueryRequest{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetAliaseList with buildAliasListResponse err", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyFunc(storage.GetAliasesByPage,
			func(ctx server.Context, funcName string, pageIndex, pageSize int) ([]storage.AliasValue, error) {
				return []storage.AliasValue{{}}, nil
			}).ApplyFunc(buildAliasResponse, func(server.Context, storage.AliasValue) (model.AliasResponse, error) {
			return model.AliasResponse{}, errors.New("mock err")
		})
		defer patch.Reset()
		_, err := GetAliaseList(ctx, model.AliasListQueryRequest{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetAliaseList", t, func() {
		ctx := fakecontext.NewMockContext()
		patch := gomonkey.ApplyFunc(storage.GetAliasesByPage,
			func(ctx server.Context, funcName string, pageIndex, pageSize int) ([]storage.AliasValue, error) {
				return []storage.AliasValue{{}}, nil
			}).ApplyFunc(buildAliasResponse, func(server.Context, storage.AliasValue) (model.AliasResponse, error) {
			return model.AliasResponse{}, nil
		})
		defer patch.Reset()
		_, err := GetAliaseList(ctx, model.AliasListQueryRequest{})
		So(err, ShouldBeNil)
	})
}

func TestDeleteAlias2(t *testing.T) {
	Convey("Test DeleteAlias2", t, func() {
		patches := gomonkey.NewPatches()
		txn := &storage.Txn{}
		patches.ApplyFunc(storage.NewTxn, func(c server.Context) *storage.Txn {
			return txn
		})
		patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
		patches.ApplyFunc(storage.GetAlias, func(*storage.Txn, string, string) (storage.AliasValue, error) {
			return storage.AliasValue{
				RoutingConfig: map[string]int{"abc": 123},
			}, nil
		})
		defer patches.Reset()
		req := model.AliasDeleteRequest{}
		ctx := fakecontext.NewMockContext()
		Convey("with AliasName != nil", func() {
			patch := gomonkey.ApplyFunc(storage.DeleteAlias, func(*storage.Txn, string, string, []string) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
				return nil
			})
			defer patch.Reset()
			req.AliasName = "abc"
			err := DeleteAlias(ctx, req)
			So(err, ShouldBeNil)
		})
		Convey("with AliasName == nil and DeleteAliasByFunctionName err", func() {
			patch := gomonkey.ApplyFunc(storage.DeleteAliasByFunctionName, func(storage.Transaction, string) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			req.AliasName = ""
			err := DeleteAlias(ctx, req)
			So(err, ShouldNotBeNil)
		})
		Convey("with AliasName == nil", func() {
			patch := gomonkey.ApplyFunc(storage.DeleteAliasByFunctionName, func(storage.Transaction, string) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			req.AliasName = ""
			err := DeleteAlias(ctx, req)
			So(err, ShouldNotBeNil)
		})

	})
}

func TestGetAlias2(t *testing.T) {
	Convey("Test GetAlias", t, func() {
		ctx := fakecontext.NewMockContext()
		txn := &storage.Txn{}
		patches := gomonkey.NewPatches()
		patches.ApplyMethod(reflect.TypeOf(txn), "Cancel", func(_ *storage.Txn) {})
		defer patches.Reset()
		Convey("with GetAlias err", func() {
			patch := gomonkey.ApplyFunc(storage.GetAlias,
				func(*storage.Txn, string, string) (storage.AliasValue, error) {
					return storage.AliasValue{}, errors.New("mock err")
				})
			defer patch.Reset()
			req := model.AliasQueryRequest{}
			_, err := GetAlias(ctx, req)
			So(err, ShouldNotBeNil)
		})
		Convey("with Commit err", func() {
			patch := gomonkey.ApplyFunc(storage.GetAlias,
				func(*storage.Txn, string, string) (storage.AliasValue, error) {
					return storage.AliasValue{}, nil
				}).ApplyMethod(reflect.TypeOf(txn), "Commit", func(_ *storage.Txn) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			req := model.AliasQueryRequest{}
			_, err := GetAlias(ctx, req)
			So(err, ShouldNotBeNil)
		})
		Convey("with buildAliasResponse err", func() {
			patch := gomonkey.ApplyFunc(storage.GetAlias,
				func(*storage.Txn, string, string) (storage.AliasValue, error) {
					return storage.AliasValue{}, nil
				}).ApplyMethod(reflect.TypeOf(txn), "Commit",
				func(_ *storage.Txn) error {
					return nil
				}).ApplyFunc(buildAliasResponse,
				func(ctx server.Context, alias storage.AliasValue) (model.AliasResponse, error) {
					return model.AliasResponse{}, errors.New("mock err")
				})
			defer patch.Reset()
			req := model.AliasQueryRequest{}
			_, err := GetAlias(ctx, req)
			So(err, ShouldNotBeNil)
		})
		Convey("with no err", func() {
			patch := gomonkey.ApplyFunc(storage.GetAlias,
				func(*storage.Txn, string, string) (storage.AliasValue, error) {
					return storage.AliasValue{}, nil
				}).ApplyMethod(reflect.TypeOf(txn), "Commit",
				func(_ *storage.Txn) error {
					return nil
				}).ApplyFunc(buildAliasResponse,
				func(ctx server.Context, alias storage.AliasValue) (model.AliasResponse, error) {
					return model.AliasResponse{}, nil
				})
			defer patch.Reset()
			req := model.AliasQueryRequest{}
			_, err := GetAlias(ctx, req)
			So(err, ShouldBeNil)
		})

	})
}
