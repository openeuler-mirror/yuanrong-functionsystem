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

import "testing"

func TestRawFormat(t *testing.T) {
	var cases = []struct {
		in       string // input
		expected string // expected result
	}{
		{"\n\tFoo\n\tBar\n", "  Foo\n  Bar\n"},
		{"\n    Foo\n    Bar\n", "  Foo\n  Bar\n"},
	}
	for _, c := range cases {
		actual := RawFormat(c.in)
		if actual != c.expected {
			t.Errorf("Exists(%s) = %v; expected %v", c.in, actual, c.expected)
		}
	}
}
