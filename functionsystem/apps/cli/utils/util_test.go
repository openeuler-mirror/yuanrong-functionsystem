/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
package utils

import (
	"runtime"
	"testing"

	"github.com/smartystreets/goconvey/convey"
)

func TestSupportSystem(t *testing.T) {
	convey.Convey("TestSupportSystem", t, func() {
		if runtime.GOOS == "linux" {
			res := SupportSystem()
			convey.So(res, convey.ShouldEqual, true)
		} else {
			res := SupportSystem()
			convey.So(res, convey.ShouldEqual, false)
		}
	})
}

func TestParamCheck(t *testing.T) {
	convey.Convey("TestParamCheck", t, func() {
		err := ParamCheck("--master-info")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("-a")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("1234")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("{\"a\": \"|;*{}[]<>\"}")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("[\"a\"]")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("-a*")
		convey.So(err, convey.ShouldNotBeNil)
		convey.So(err.Error(), convey.ShouldEqual, "invalid parameter field: -a*")

		err = ParamCheck("{;}")
		convey.So(err, convey.ShouldNotBeNil)
		convey.So(err.Error(), convey.ShouldEqual, "invalid parameter value: {;}")

		err = ParamCheck("<>")
		convey.So(err, convey.ShouldNotBeNil)
		convey.So(err.Error(), convey.ShouldEqual, "invalid parameter value: <>")

		err = ParamCheck("-a=")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("--a=b")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("--a={}")
		convey.So(err, convey.ShouldBeNil)

		err = ParamCheck("--a*=b")
		convey.So(err, convey.ShouldNotBeNil)
		convey.So(err.Error(), convey.ShouldEqual, "invalid parameter: --a*=b")

		err = ParamCheck("--a=*")
		convey.So(err, convey.ShouldNotBeNil)
		convey.So(err.Error(), convey.ShouldEqual, "invalid parameter: --a=*")
	})
}
