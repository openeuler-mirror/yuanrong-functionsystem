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

package utils

import (
	"regexp"

	"github.com/asaskevich/govalidator/v11"
)

const funNameReg = "(^0-[0-9a-zA-Z]{1,16}-([a-z0-9][a-z0-9-]{0,126}[a-z0-9]|" +
	"[a-z])$)|(^[a-z][a-z0-9-]{0,126}[a-z0-9]$|^[a-z]$)|(^0@[0-9a-zA-Z]{1,16}@([a-z0-9][a-z0-9-]{0," +
	"126}[a-z0-9]|[a-z])$)"

const poolIDReg = "^[a-z0-9]([-a-z0-9]{0,38}[a-z0-9])?$"

func init() {
	govalidator.CustomTypeTagMap.Set("functionName",
		func(i interface{}, o interface{}) bool {
			switch v := i.(type) {
			case string:
				if ValidateFunctionName(v, funNameReg) {
					return true
				}
			default:
				return false
			}
			return false
		})
	govalidator.CustomTypeTagMap.Set("poolId",
		func(i interface{}, o interface{}) bool {
			switch v := i.(type) {
			case string:
				if ValidatePoolID(v) {
					return true
				}
			default:
				return false
			}
			return false
		})
}

// ValidateFunctionName is Validator for function name
// if Matches return true
func ValidateFunctionName(n string, reg string) bool {
	isMatch, err := regexp.MatchString(reg, n)
	if err != nil {
		return false
	}
	return isMatch
}

// ValidatePoolID is Validator for poolID
func ValidatePoolID(n string) bool {
	if len(n) == 0 {
		return true
	}
	isMatch, err := regexp.MatchString(poolIDReg, n)
	if err != nil {
		return false
	}
	return isMatch
}
