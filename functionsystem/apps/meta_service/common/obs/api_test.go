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

// Package obs is the obs client
package obs

import (
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/huaweicloud/huaweicloud-sdk-go-obs/obs"
	"github.com/stretchr/testify/assert"
)

func TestNewObsClient(t *testing.T) {
	conf := Option{}
	conf.Secure = true
	_, err := NewObsClient(conf)
	assert.Equal(t, nil, err)

	conf.Secure = false
	_, err = NewObsClient(conf)
	assert.NotEqual(t, nil, err)
}

func Test_getEndpoint(t *testing.T) {
	endpoint := httpPrefix
	endpoint, urlPrefix := getEndpoint(endpoint)
	assert.Equal(t, httpPrefix, urlPrefix)

	endpoint = httpsPrefix
	endpoint, urlPrefix = getEndpoint(endpoint)
	assert.Equal(t, httpsPrefix, urlPrefix)
}

func TestClearSecretKeyMemory(t *testing.T) {
	ClearSecretKeyMemory()
}

func TestGetTLSConfig(t *testing.T) {
	getTLSConfig("", true)
}
