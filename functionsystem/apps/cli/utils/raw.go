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
	"strings"
)

// RawFormat format raw string
//     raw := `
//     	line
//     	Bar
//     `
// Is equivalent to "\n\tFoo\n\tBar\n" or "\n    Foo\n    Bar\n"
// I want to convert it to "  Foo\n  Bar\n"
func RawFormat(raw string) string {
	if len(raw) == 0 {
		return ""
	}

	// Remove the first line \n
	if raw[0] == '\n' {
		raw = raw[1:]
	}
	// split raw by \n
	lines := strings.Split(raw, "\n")

	// convert \t at the beginning of each line to two space indentation
	for i, line := range lines {
		if strings.HasPrefix(line, "\t") {
			lines[i] = strings.Replace(line, "\t", "  ", 1)
		}
		if strings.HasPrefix(line, "    ") {
			lines[i] = strings.Replace(line, "    ", "  ", 1)
		}
	}

	return strings.Join(lines, "\n")
}
