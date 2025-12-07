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
	"fmt"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/assertions/should"
	. "github.com/smartystreets/goconvey/convey"

	"meta_service/function_repo/model"
	"meta_service/function_repo/storage"
	"meta_service/function_repo/test/fakecontext"
	"meta_service/function_repo/utils/constants"
)

func Test_updatePodPool(t *testing.T) {
	Convey("Test update pool with deleted status", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Deleted
			return pool, nil
		})
		defer patches.Reset()
		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1"})
		So(err, ShouldNotBeNil)
	})
	Convey("Test update pool with new status", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.New
			return pool, nil
		})
		defer patches.Reset()
		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1"})
		So(err, ShouldNotBeNil)
	})
	Convey("Test update pool with update status", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Update
			return pool, nil
		})
		defer patches.Reset()
		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1"})
		So(err, ShouldNotBeNil)
	})
	Convey("Test update pool with failed status", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Failed
			return pool, nil
		})
		defer patches.Reset()

		patche1s := gomonkey.ApplyFunc(storage.CreateUpdatePodPool, func(storage.Transaction, storage.PoolValue) error {
			return fmt.Errorf("update pod pool failed")
		})
		defer patche1s.Reset()
		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1"})
		So(err.Error(), ShouldEqual, "update pod pool failed")
	})
	Convey("Test update pool max size less than 0", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Running
			pool.Scalable = true
			pool.Size = 1
			return pool, nil
		})
		defer patches.Reset()

		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1", MaxSize: -2})
		So(err.Error(), ShouldEqual, "failed to update pod pool, err: max_size is less than 0")
	})
	Convey("Test update pool max size less than size", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Running
			pool.Scalable = true
			pool.Size = 1
			return pool, nil
		})
		defer patches.Reset()

		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1", MaxSize: 1, Size: 2,
			HorizontalPodAutoscalerSpec: "TEST"})
		So(err.Error(), ShouldEqual, "failed to update pod pool, err: max_size is less than size")
	})
	Convey("Test update pool max size and horizontal_pod_autoscaler_spec", t, func() {
		patches := gomonkey.ApplyFunc(storage.GetPodPool, func(storage.Transaction, string) (storage.PoolValue, error) {
			pool := storage.PoolValue{}
			pool.Status = constants.Running
			pool.Scalable = true
			pool.Size = 1
			return pool, nil
		})
		defer patches.Reset()

		ctx := fakecontext.NewMockContext()
		err := UpdatePodPool(ctx, model.PodPoolUpdateRequest{ID: "pool1", MaxSize: 2, Size: 1,
			HorizontalPodAutoscalerSpec: "TEST"})
		So(err.Error(), ShouldEqual, "failed to update pod pool, err: max_size greater than size and horizontal_pod_autoscaler_spec cannot be set at the same time")
	})

}

func Test_createPodPool(t *testing.T) {
	Convey("Test create pool error", t, func() {
		ctx := fakecontext.NewMockContext()
		_, err := CreatePodPool(ctx, model.PodPoolCreateRequest{Pools: []model.Pool{{Id: "pool1", Size: 2,
			MaxSize: 1}}})
		So(err.Error(), should.ContainSubstring, "err: max_size is less than size")
		_, err = CreatePodPool(ctx, model.PodPoolCreateRequest{Pools: []model.Pool{{Id: "pool1", Size: 2,
			MaxSize: 3, HorizontalPodAutoscalerSpec: "test"}}})
		So(err.Error(), should.ContainSubstring, "max_size greater than size and horizontal_pod_autoscaler_spec cannot be set at the same time")
		_, err = CreatePodPool(ctx, model.PodPoolCreateRequest{Pools: []model.Pool{{Id: "pool1", Size: 2,
			MaxSize: 3, IdleRecycleTime: model.IdleRecyclePolicy{Reserved: -2}}}})
		So(err.Error(), should.ContainSubstring, "err: idle time of reserved should not less than")
		_, err = CreatePodPool(ctx, model.PodPoolCreateRequest{Pools: []model.Pool{{Id: "pool1", Size: 2,
			MaxSize: 3, IdleRecycleTime: model.IdleRecyclePolicy{Scaled: -2}}}})
		So(err.Error(), should.ContainSubstring, "err: idle time of scaled should not less than")

	})
}
