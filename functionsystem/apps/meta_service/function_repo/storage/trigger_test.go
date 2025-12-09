package storage

import (
	"errors"
	"reflect"
	"testing"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/test/fakecontext"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
)

func TestGetTriggersByFunctionName(t *testing.T) {
	Convey("Test GetTriggersByFunctionName with tenantInfo nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		_, err := GetTriggersByFunctionName(txn, "abc123")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetTriggersByFunctionName with mock err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerGetRange",
			func(t *generatedTx, prefix TriggerKey) ([]TriggerTuple, error) {
				return nil, errors.New("mock err")
			})
		defer patch.Reset()
		_, err := GetTriggersByFunctionName(txn, "abc123")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetTriggersByFunctionName ", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerGetRange",
			func(t *generatedTx, prefix TriggerKey) ([]TriggerTuple, error) {
				return nil, nil
			})
		defer patch.Reset()
		_, err := GetTriggersByFunctionName(txn, "abc123")
		So(err, ShouldBeNil)
	})
}

func TestGetFunctionInfo(t *testing.T) {
	Convey("Test GetFunctionInfo with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		_, err := GetFunctionInfo(txn, "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetFunctionInfo with err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerFunctionIndexGet",
			func(t *generatedTx, key TriggerFunctionIndexKey) (TriggerFunctionIndexValue, error) {
				return TriggerFunctionIndexValue{}, errors.New("mock err")
			})
		defer patch.Reset()
		_, err := GetFunctionInfo(txn, "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
	Convey("Test GetFunctionInfo", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerFunctionIndexGet",
			func(t *generatedTx, key TriggerFunctionIndexKey) (TriggerFunctionIndexValue, error) {
				return TriggerFunctionIndexValue{}, nil
			})
		defer patch.Reset()
		_, err := GetFunctionInfo(txn, "mockTriggerID")
		So(err, ShouldBeNil)
	})
}

func TestDeleteTriggerByFunctionNameVersion(t *testing.T) {
	Convey("Test DeleteTriggerByFunctionNameVersion with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteTriggerByFunctionNameVersion(txn, "mockName", "mockVerOrAlias")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteTriggerByFunctionNameVersion with TriggerGetRange err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerGetRange",
			func(t *generatedTx, prefix TriggerKey) ([]TriggerTuple, error) {
				return nil, errors.New("mock TriggerGetRange err")
			})
		defer patch.Reset()
		err := DeleteTriggerByFunctionNameVersion(txn, "mockName", "mockVerOrAlias")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteTriggerByFunctionNameVersion with TriggerFunctionIndexDelete", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerGetRange",
			func(t *generatedTx, prefix TriggerKey) ([]TriggerTuple, error) {
				return []TriggerTuple{{TriggerKey{}, TriggerValue{}}}, nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "TriggerFunctionIndexDelete",
			func(t *generatedTx, key TriggerFunctionIndexKey) error {
				return errors.New("mock TriggerFunctionIndexDelete err")
			})
		defer patch.Reset()
		err := DeleteTriggerByFunctionNameVersion(txn, "mockName", "mockVerOrAlias")
		So(err, ShouldNotBeNil)
	})
}

func TestDeleteTrigger(t *testing.T) {
	Convey("Test DeleteTrigger with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteTrigger(txn, "mockFunc", "mockVerOrAlias", "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteTrigger with GetTriggerInfo err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyFunc(GetTriggerInfo, func(_ *Txn, _ string, _ string, _ string) (TriggerValue, error) {
			return TriggerValue{}, errors.New("mock GetTriggerInfo err")
		})
		defer patch.Reset()
		err := DeleteTrigger(txn, "mockFunc", "mockVerOrAlias", "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteTrigger with txn.txn.TriggerDelete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyFunc(GetTriggerInfo, func(_ *Txn, _ string, _ string, _ string) (TriggerValue, error) {
			return TriggerValue{}, nil
		}).ApplyMethod(reflect.TypeOf(txn.txn), "TriggerDelete",
			func(t *generatedTx, key TriggerKey) error {
				return errors.New("mock TriggerDelete err")
			})
		defer patch.Reset()
		err := DeleteTrigger(txn, "mockFunc", "mockVerOrAlias", "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteTrigger with txn.txn.TriggerFunctionIndexDelete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyFunc(GetTriggerInfo,
			func(_ *Txn, _ string, _ string, _ string) (TriggerValue, error) {
				return TriggerValue{}, nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "TriggerDelete",
			func(t *generatedTx, key TriggerKey) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "TriggerFunctionIndexDelete",
			func(t *generatedTx, key TriggerFunctionIndexKey) error {
				return errors.New("mock TriggerFunctionIndexDelete err")
			})
		defer patch.Reset()
		err := DeleteTrigger(txn, "mockFunc", "mockVerOrAlias", "mockTriggerID")
		So(err, ShouldNotBeNil)
	})
}

func TestSaveTriggerInfo(t *testing.T) {
	Convey("Test SaveTriggerInfo with TenantInfo err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := SaveTriggerInfo(txn, "func-name", "ver-or-alias", "trigger-id", TriggerValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test SaveTriggerInfo with TriggerPut err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patches := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerPut", func(t *generatedTx, key TriggerKey, val TriggerValue) error {
			return errors.New("mock err")
		})
		defer patches.Reset()
		err := SaveTriggerInfo(txn, "func-name", "ver-or-alias", "trigger-id", TriggerValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test SaveTriggerInfo with TriggerFunctionIndexPut err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patches := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "TriggerPut", func(t *generatedTx, key TriggerKey, val TriggerValue) error {
			return nil
		}).ApplyMethod(reflect.TypeOf(txn.txn), "TriggerFunctionIndexPut", func(t *generatedTx, key TriggerFunctionIndexKey, val TriggerFunctionIndexValue) error {
			return errors.New("mock err")
		})
		defer patches.Reset()
		err := SaveTriggerInfo(txn, "func-name", "ver-or-alias", "trigger-id", TriggerValue{})
		So(err, ShouldNotBeNil)
	})
}

func TestCreateAlias2(t *testing.T) {
	Convey("Test CreateAlias 2", t, func() {
		Convey("with TenantInfo err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewContext(),
			}
			err := CreateAlias(txn, AliasValue{})
			So(err, ShouldNotBeNil)
		})
		Convey("with AliasPut err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewMockContext(),
			}
			patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasPut", func(
				t *generatedTx, key AliasKey, val AliasValue,
			) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			err := CreateAlias(txn, AliasValue{RoutingConfig: map[string]int{"a": 1}})
			So(err, ShouldNotBeNil)
		})
		Convey("with AliasRoutingIndexPut err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewMockContext(),
			}
			patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "AliasPut", func(
				t *generatedTx, key AliasKey, val AliasValue,
			) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "AliasRoutingIndexPut", func(
				t *generatedTx, key AliasRoutingIndexKey, val AliasRoutingIndexValue,
			) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			err := CreateAlias(txn, AliasValue{RoutingConfig: map[string]int{"a": 1}})
			So(err, ShouldNotBeNil)
		})
	})
}

func Test_save(t *testing.T) {
	Convey("Test HTTPTriggerEtcdSpec.save", t, func() {
		spec := &HTTPTriggerEtcdSpec{}
		Convey("with TenantInfo err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewContext(),
			}
			err := spec.save(txn, "mock-name", "mock-ver")
			So(err, ShouldNotBeNil)
		})
		Convey("with functionID and resourceID exists err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewMockContext(),
			}
			patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexGet", func(
				t *generatedTx, key FunctionResourceIDIndexKey,
			) (FunctionResourceIDIndexValue, error) {
				return FunctionResourceIDIndexValue{}, nil
			})
			defer patch.Reset()
			err := spec.save(txn, "mock-name", "mock-ver")
			So(err, ShouldNotBeNil)
		})
		Convey("with FunctionResourceIDIndexGet err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewMockContext(),
			}
			patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexGet", func(
				t *generatedTx, key FunctionResourceIDIndexKey,
			) (FunctionResourceIDIndexValue, error) {
				return FunctionResourceIDIndexValue{}, errors.New("mock err")
			})
			defer patch.Reset()
			err := spec.save(txn, "mock-name", "mock-ver")
			So(err, ShouldNotBeNil)
		})
		Convey("with FunctionResourceIDIndexPut err", func() {
			txn := &Txn{
				txn: &generatedTx{},
				c:   fakecontext.NewMockContext(),
			}
			patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexGet", func(
				t *generatedTx, key FunctionResourceIDIndexKey,
			) (FunctionResourceIDIndexValue, error) {
				return FunctionResourceIDIndexValue{}, errmsg.KeyNotFoundError
			}).ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexPut", func(
				t *generatedTx, key FunctionResourceIDIndexKey, val FunctionResourceIDIndexValue,
			) error {
				return errors.New("mock err")
			})
			defer patch.Reset()
			err := spec.save(txn, "mock-name", "mock-ver")
			So(err, ShouldNotBeNil)
		})
	})
}
