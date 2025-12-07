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
package zap

import (
	"testing"

	"meta_service/common/logger/config"

	"github.com/stretchr/testify/assert"
)

// TestNewDevelopmentLog Test New Development Log
func TestNewDevelopmentLog(t *testing.T) {
	if _, err := NewDevelopmentLog(); err != nil {
		t.Errorf("NewDevelopmentLog() = %q, wants *logger", err)
	}
}

func TestNewProductionLog(t *testing.T) {
	_, err := NewProductionLog("")
	assert.NotNil(t, err)

	_, err = NewProductionLog("test")
	assert.Nil(t, err)
}

func TestNewConsoleLog(t *testing.T) {
	_, err := NewConsoleLog()
	assert.Nil(t, err)
}

func TestNewWithLevel(t *testing.T) {
	_, err := NewWithLevel(config.CoreInfo{})
	assert.NotNil(t, err)
}

func TestNewCore(t *testing.T) {
	coreInfo := config.CoreInfo{}
	newCore(coreInfo)
}
