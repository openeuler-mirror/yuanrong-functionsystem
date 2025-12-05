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
	"encoding/json"
	"errors"
	"regexp"
	"runtime"
	"strings"
)

var invalidRe *regexp.Regexp = regexp.MustCompile("[$&!?*;<>{}|`\n\\[\\]\\\\]")

// SupportSystem 当前只支持Linux
func SupportSystem() bool {
	return runtime.GOOS == "linux"
}

// ParamCheck command parameter invalid character check
func ParamCheck(param string) error {
	if len(param) == 0 || invalidRe == nil {
		return nil
	}
	if !strings.HasPrefix(param, "-") {
		// value check
		if !invalidRe.MatchString(param) {
			return nil
		}
		if json.Valid([]byte(param)) {
			// json pattern
			return nil
		}
		return errors.New("invalid parameter value: " + param)
	}

	index := strings.Index(param, "=")
	if index == -1 {
		if invalidRe.MatchString(param) {
			return errors.New("invalid parameter field: " + param)
		}
		return nil
	}

	field := param[:index]
	// field check
	if invalidRe.MatchString(field) {
		return errors.New("invalid parameter: " + param)
	}

	if index != len(param)-1 {
		value := param[index+1:]
		// value check
		if !invalidRe.MatchString(value) {
			return nil
		}
		if json.Valid([]byte(value)) {
			// json pattern
			return nil
		}
		return errors.New("invalid parameter: " + param)
	}
	return nil
}
