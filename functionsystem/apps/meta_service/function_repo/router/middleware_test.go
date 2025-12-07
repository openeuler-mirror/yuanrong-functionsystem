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

// Package router
package router

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestIsUploadCode(t *testing.T) {
	for _, test := range []struct {
		contentType string
		is          bool
		info        string
	}{
		{"application/vnd.yuanrong+attachment;file-size=683;revision-id=12345678901234567890" +
			"12345678901234567890123456789012345678901234567890123456789012345678901234567890", false, ""},
		{"", false, ""},
		{"application/json", false, ""},
		{"application/vnd.yuanrong+attachment;file-size=683;revision-id=1234567890",
			true, "file-size=683&revision-id=1234567890"},
	} {
		ok, info := isUploadCode(test.contentType)
		assert.Equal(t, test.is, ok)
		assert.Equal(t, test.info, info)
	}
}
