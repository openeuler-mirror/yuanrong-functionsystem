package storage

import (
	"context"
	"errors"
	"reflect"
	"testing"

	"meta_service/function_repo/test/fakecontext"

	"github.com/agiledragon/gomonkey"
	. "github.com/smartystreets/goconvey/convey"
)

func TestUpdateLayer(t *testing.T) {
	Convey("Test UpdateLayer with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := UpdateLayer(txn, "mockLayer", 1, LayerValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test UpdateLayer with LayerPut err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerPut",
			func(t *generatedTx, key LayerKey, val LayerValue) error {
				return errors.New("mock LayerPut err")
			})
		defer patch.Reset()
		err := UpdateLayer(txn, "mockLayer", 1, LayerValue{})
		So(err, ShouldNotBeNil)
	})
}

func TestDeleteLayer(t *testing.T) {
	Convey("Test DeleteLayer with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteLayer(txn, "mockLayer")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteLayer with LayerDeleteRange err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerDeleteRange",
			func(t *generatedTx, prefix LayerKey) error {
				return errors.New("mock LayerDeleteRange err")
			})
		defer patch.Reset()
		err := DeleteLayer(txn, "mockLayer")
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteLayer with LayerCountIndexDelete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerDeleteRange",
			func(t *generatedTx, prefix LayerKey) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerCountIndexDelete",
			func(t *generatedTx, key LayerCountIndexKey) error {
				return errors.New("mock LayerCountIndexDelete err")
			})
		defer patch.Reset()
		err := DeleteLayer(txn, "mockLayer")
		So(err, ShouldNotBeNil)
	})
}

func TestDeleteLayerVersion(t *testing.T) {
	Convey("Test DeleteLayerVersion with tenant nil", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := DeleteLayerVersion(txn, "mockLayer", 0)
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteLayerVersion with LayerDelete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerDelete",
			func(t *generatedTx, key LayerKey) error {
				return errors.New("mock LayerDelete err")
			})
		defer patch.Reset()

		err := DeleteLayerVersion(txn, "mockLayer", 0)
		So(err, ShouldNotBeNil)
	})

	Convey("Test DeleteLayerVersion with LayerGetRange err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerDelete",
			func(t *generatedTx, key LayerKey) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerGetRange",
			func(t *generatedTx, _ LayerKey) ([]LayerTuple, error) {
				return []LayerTuple{}, errors.New("mock LayerGetRange err")
			})
		defer patch.Reset()

		err := DeleteLayerVersion(txn, "mockLayer", 0)
		So(err, ShouldNotBeNil)
	})
	Convey("Test DeleteLayerVersion with LayerCountIndexDelete err", t, func() {
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerDelete",
			func(t *generatedTx, key LayerKey) error {
				return nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerGetRange",
			func(t *generatedTx, _ LayerKey) ([]LayerTuple, error) {
				return []LayerTuple{}, nil
			}).ApplyMethod(reflect.TypeOf(txn.txn), "LayerCountIndexDelete",
			func(t *generatedTx, key LayerCountIndexKey) error {
				return errors.New("mock LayerCountIndexDelete err")
			})
		defer patch.Reset()

		err := DeleteLayerVersion(txn, "mockLayer", 0)
		So(err, ShouldNotBeNil)
	})
}

func TestHTTPTriggerEtcdSpecDelete(t *testing.T) {
	Convey("Test HTTPTriggerEtcdSpec.delete with tenant nil", t, func() {
		httpTriggerEtcdSpec := &HTTPTriggerEtcdSpec{}
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewContext(),
		}
		err := httpTriggerEtcdSpec.delete(txn, "mockFunc", "mockVerOrAlias")
		So(err, ShouldNotBeNil)
	})
	Convey("Test HTTPTriggerEtcdSpec.delete with FunctionResourceIDIndexDelete err", t, func() {
		httpTriggerEtcdSpec := &HTTPTriggerEtcdSpec{}
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexDelete",
			func(t *generatedTx, key FunctionResourceIDIndexKey) error {
				return errors.New("mock FunctionResourceIDIndexDelete err")
			})
		defer patch.Reset()
		err := httpTriggerEtcdSpec.delete(txn, "mockFunc", "mockVerOrAlias")
		So(err, ShouldNotBeNil)
	})
	Convey("Test HTTPTriggerEtcdSpec.delete with FunctionResourceIDIndexDeleteRange err", t, func() {
		httpTriggerEtcdSpec := &HTTPTriggerEtcdSpec{}
		txn := &Txn{
			txn: &generatedTx{},
			c:   fakecontext.NewMockContext(),
		}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "FunctionResourceIDIndexDeleteRange",
			func(t *generatedTx, prefix FunctionResourceIDIndexKey) error {
				return errors.New("mock FunctionResourceIDIndexDeleteRange err")
			})
		defer patch.Reset()
		err := httpTriggerEtcdSpec.delete(txn, "mockFunc", "")
		So(err, ShouldNotBeNil)
	})
}

func TestCreateLayer(t *testing.T) {
	Convey("Test CreateLayer with TenantInfo err", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewContext()}
		err := CreateLayer(txn, "mock-layer", 1, LayerValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateLayer with LayerCountIndexPut err", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewMockContext()}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerCountIndexPut", func(
			t *generatedTx, key LayerCountIndexKey, val LayerCountIndexValue,
		) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := CreateLayer(txn, "mock-layer", 1, LayerValue{})
		So(err, ShouldNotBeNil)
	})
	Convey("Test CreateLayer with LayerPut err", t, func() {
		txn := &Txn{txn: &generatedTx{}, c: fakecontext.NewMockContext()}
		patch := gomonkey.ApplyMethod(reflect.TypeOf(txn.txn), "LayerPut", func(
			t *generatedTx, key LayerKey, val LayerValue,
		) error {
			return errors.New("mock err")
		})
		defer patch.Reset()
		err := CreateLayer(txn, "mock-layer", 2, LayerValue{})
		So(err, ShouldNotBeNil)
	})
}

func TestGetLayerLatestVersion(t *testing.T) {
	Convey("Test GetLayerLatestVersion", t, func() {
		Convey("with TenantInfo err", func() {
			ctx := fakecontext.NewContext()
			_, _, err := GetLayerLatestVersion(ctx, "mock-layer-name")
			So(err, ShouldNotBeNil)
		})
		Convey("with LayerCountIndexGet err", func() {
			ctx := fakecontext.NewMockContext()
			patches := gomonkey.ApplyGlobalVar(&db, &generatedKV{}).ApplyMethod(reflect.TypeOf(&generatedKV{}), "LayerCountIndexGet", func(
				kv *generatedKV, ctx context.Context, key LayerCountIndexKey,
			) (LayerCountIndexValue, error) {
				return LayerCountIndexValue{}, errors.New("mock err")
			})
			defer patches.Reset()
			_, _, err := GetLayerLatestVersion(ctx, "mock-layer-name")
			So(err, ShouldNotBeNil)
		})
		Convey("with LayerLastInRange err", func() {
			ctx := fakecontext.NewMockContext()
			patches := gomonkey.ApplyGlobalVar(&db, &generatedKV{}).ApplyMethod(reflect.TypeOf(&generatedKV{}), "LayerCountIndexGet", func(
				kv *generatedKV, ctx context.Context, key LayerCountIndexKey,
			) (LayerCountIndexValue, error) {
				return LayerCountIndexValue{}, nil
			}).ApplyMethod(reflect.TypeOf(&generatedKV{}), "LayerLastInRange", func(
				kv *generatedKV, ctx context.Context, prefix LayerKey,
			) (LayerKey, LayerValue, error) {
				return LayerKey{}, LayerValue{}, errors.New("mock err")
			})
			defer patches.Reset()
			_, _, err := GetLayerLatestVersion(ctx, "mock-layer-name")
			So(err, ShouldNotBeNil)
		})
	})
}
