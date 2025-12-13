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
	"bytes"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestNewTable(t *testing.T) {
	testCases := []struct {
		name    string
		headers []string
		bulks   [][]string
		wantStd string
	}{
		{
			name:    "test",
			headers: []string{"test"},
			bulks:   [][]string{{"test"}},
			wantStd: `+------+
| TEST |
+------+
| test |
+------+
`,
		},
	}
	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			out := &bytes.Buffer{}
			table := NewTable(out, true, true, true)
			table.SetHeader(tt.headers)
			table.AppendBulk(tt.bulks)
			table.Render()
			assert.Equal(t, tt.wantStd, out.String())
		})
	}
}
