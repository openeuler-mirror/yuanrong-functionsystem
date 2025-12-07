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

package model

import (
	"encoding/json"
	"errors"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"
)

func TestTriggerMarshal(t *testing.T) {
	convey.Convey("TestValidateAlias", t, func() {
		convey.Convey("test trigger create request", func() {
			r := &TriggerCreateRequest{
				TriggerType: HTTPType,
			}
			ret, err := r.MarshalJSON()
			convey.ShouldNotBeNil(err)
			err = r.UnmarshalJSON(ret)
			convey.ShouldNotBeNil(err)
		})

		convey.Convey("test trigger info", func() {
			r := &TriggerInfo{}
			ret, err := r.MarshalJSON()
			convey.ShouldNotBeNil(err)
			err = r.UnmarshalJSON(ret)
			convey.ShouldNotBeNil(err)
		})

		convey.Convey("test trigger update request", func() {
			r := &TriggerUpdateRequest{}
			ret, err := r.MarshalJSON()
			convey.ShouldNotBeNil(err)
			err = r.UnmarshalJSON(ret)
			convey.ShouldNotBeNil(err)
		})
	})
}

func TestTriggerMarshalFailed(t *testing.T) {
	convey.Convey("TestValidateAliasFailed", t, func() {
		convey.Convey("test trigger create request failed", func() {
			r := &TriggerCreateRequest{}

			patches := gomonkey.NewPatches()
			defer patches.Reset()
			patches.ApplyFunc(json.Marshal, func(v interface{}) ([]byte, error) {
				return []byte("aa"), errors.New("test error")
			})
			patches.ApplyFunc(json.Unmarshal, func(data []byte, v interface{}) error {
				return errors.New("test error")
			})

			ret, err := r.MarshalJSON()
			convey.ShouldNotBeNil(err)
			err = r.UnmarshalJSON(ret)
			convey.ShouldNotBeNil(err)
		})

	})
}
