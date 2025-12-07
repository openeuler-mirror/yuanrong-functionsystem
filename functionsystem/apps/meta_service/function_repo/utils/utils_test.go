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
	"encoding/json"
	"testing"

	"github.com/stretchr/testify/assert"

	"meta_service/common/constants"
	"meta_service/common/crypto"
)

func TestGetUTCRevisionId(t *testing.T) {
	id := GetUTCRevisionID()
	assert.NotEqual(t, "", id)
}

func TestInterceptNameAndVersionFromLayerURN(t *testing.T) {
	s, i := InterceptNameAndVersionFromLayerURN("sn:cn:yrk:i1fe539427b24702acc11fbb4e134e17:layer:aa:1")
	assert.Equal(t, "aa", s)
	assert.Equal(t, "1", i)

	s, i = InterceptNameAndVersionFromLayerURN("sn:cn:yrk")
	assert.Equal(t, "", s)
	assert.Equal(t, "", i)
}

func TestValidateFunctionName(t *testing.T) {
	b := ValidateFunctionName("0-ss-ss")
	assert.Equal(t, true, b)

	b = ValidateFunctionName("@-s-11-11")
	assert.Equal(t, false, b)

	b = ValidateFunctionName("")
	assert.Equal(t, false, b)

	b = ValidateFunctionName("0@base@1test-faas")
	assert.Equal(t, true, b)

	b = ValidateFunctionName("0-base-1test-faas")
	assert.Equal(t, true, b)
}

func TestPoolID(t *testing.T) {
	b := ValidatePoolID("0-ss-ss")
	assert.Equal(t, true, b)

	b = ValidateFunctionName("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
	assert.Equal(t, false, b)

	b = ValidateFunctionName("-aab-aa")
	assert.Equal(t, false, b)

	b = ValidateFunctionName("CCCC")
	assert.Equal(t, false, b)
}

func TestRemoveServiceID(t *testing.T) {
	n := RemoveServiceID("0-ss-aa-11")
	assert.Equal(t, "aa-11", n)
}

func TestMetadataPrefix(t *testing.T) {
	prefix := MetadataPrefix()
	t.Logf(prefix)
	assert.Equal(t, "/repo", prefix)
}

// NowTimeF -
func TestNowTimeF(t *testing.T) {
	t.Logf("%s", NowTimeF())
}

func TestIsLatestVersion(t *testing.T) {
	assert.Equal(t, true, IsLatestVersion(""))
	assert.Equal(t, true, IsLatestVersion("$latest"))
	assert.Equal(t, false, IsLatestVersion("1"))
	assert.Equal(t, false, IsLatestVersion("alias1"))
}

func TestEncryptETCDValue(t *testing.T) {
	text := "abcdef"
	_, err := EncryptETCDValue(text)
	if err != nil {
		t.Errorf("failed to encry etcd value, err: %s", err.Error())
	}
	text = ""
	_, err = EncryptETCDValue(text)
	if err == nil {
		t.Errorf("failed to encry etcd value, err: %s", err.Error())
	}
}

func TestDecryptETCDValue(t *testing.T) {
	var env = make(map[string]string, constants.DefaultMapSize)
	str := "418213bcfc5c5d10e046fbfc:48f48e7562a65ad14aa3031c2af432b289a4a837239bb571801b"
	key, _ := DecryptETCDValue("418213bcfc5c5d10e046fbfc:48f48e7562a65ad14aa3031c2af432b289a4a837239bb571801b")
	value, _ := crypto.Decrypt([]byte(str), []byte(key))
	_ = json.Unmarshal([]byte(value), env)
	assert.Equal(t, env["faas-baas"], "")
}

func TestDecodeEnv(t *testing.T) {
	str := "7da374d89a8e872bac706113:d7fb221574a0cf6c8e04b0647545f42c83f5"
	_, err := DecodeEnv(str)
	if err != nil {
		t.Errorf("failed to decode env, err:%s", err.Error())
	}

	str = "087913db14a3e6d589e818a6:d0713a17d1e07b1d7e553ad23e0778acd7c3b484b96e94016a56"
	_, err = DecodeEnv(str)
	if err == nil {
		t.Errorf("get unexpeced result")
	}

	str = "ggg"
	_, err = DecodeEnv(str)
	if err == nil {
		t.Errorf("get unexpeced result")
	}
}

func TestEncodeEnv(t *testing.T) {
	env := map[string]string{
		"a": "aa",
		"b": "bb",
	}
	_, err := EncodeEnv(env)
	if err != nil {
		t.Errorf("failed to encode env, err:%s", err.Error())
	}
}
