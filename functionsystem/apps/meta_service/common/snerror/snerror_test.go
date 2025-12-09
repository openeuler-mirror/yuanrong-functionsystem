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

// Package snerror is basic information contained in the SN error.
package snerror

import (
	"encoding/json"
	"errors"
	"fmt"
	"testing"

	"github.com/stretchr/testify/assert"
)

// TestNewError
func TestNewError(t *testing.T) {

	e := New(InternalError, InternalErrorMsg)
	fmt.Println(e.Code())
	assert.Equal(t, InternalError, e.Code())

	e = New(InternalErrorRetry, InternalErrorMsg)
	fmt.Println(e.Code())
	assert.Equal(t, InternalErrorRetry, e.Code())

	e = NewWithFmtMsg(InternalError, "test %s,%d", "test", 1)
	fmt.Println(e.Code())
	assert.Equal(t, InternalError, e.Code())

	r := ConvertError(e)
	assert.Equal(t, InternalError, r.Code)

	e = NewWithError(10086, errors.New("aaa"))
	fmt.Println(e.Code())
	assert.Equal(t, 10086, e.Code())
	assert.Equal(t, false, IsUserError(e))
}

func TestNewErrorAgain(t *testing.T) {
	e := New(ClientInsExceptionCode, ClientInsException)
	fmt.Println(e.Code())
	assert.Equal(t, ClientInsExceptionCode, e.Code())

	e = New(MaxRequestBodySizeErr, MaxRequestBodySizeMsg)
	fmt.Println(e.Code())
	assert.Equal(t, MaxRequestBodySizeErr, e.Code())

	e = New(UnableSpecifyResourceCode, UnableSpecifyResourceMsg)
	fmt.Println(e.Code())
	assert.Equal(t, UnableSpecifyResourceCode, e.Code())

	e = NewWithFmtMsg(ClientInsExceptionCode, "test %s,%d", "test", 1)
	fmt.Println(e.Code())
	assert.Equal(t, ClientInsExceptionCode, e.Code())
	r := ConvertError(e)
	assert.Equal(t, ClientInsExceptionCode, r.Code)

	e = NewWithFmtMsg(MaxRequestBodySizeErr, "test %s,%d", "test", 1)
	fmt.Println(e.Code())
	assert.Equal(t, MaxRequestBodySizeErr, e.Code())
	r = ConvertError(e)
	assert.Equal(t, MaxRequestBodySizeErr, r.Code)

	e = NewWithFmtMsg(UnableSpecifyResourceCode, "test %s,%d", "test", 1)
	fmt.Println(e.Code())
	assert.Equal(t, UnableSpecifyResourceCode, e.Code())
	r = ConvertError(e)
	assert.Equal(t, UnableSpecifyResourceCode, r.Code)

	e = NewWithError(10000, errors.New("aaa"))
	fmt.Println(e.Code())
	assert.Equal(t, 10000, e.Code())
	assert.Equal(t, false, IsUserError(e))
}

// TestError
func TestError(t *testing.T) {
	err := errors.New("aaa")
	fmt.Println(err.Error())
	e := NewWithError(10086, errors.New("aaa"))
	fmt.Println(e.Code())
	fmt.Println(e.Error())
	assert.Equal(t, 10086, e.Code())

	e1 := getErr()
	fmt.Println(e1.Error())

	e2 := getErr().(SNError)
	fmt.Println(e2.Error())
	fmt.Println(e2.Code())
	assert.Equal(t, 10086, e.Code())
}

func getErr() error {
	return NewWithError(10086, errors.New("aaa"))
}

// TestMarshal
func TestMarshal(t *testing.T) {
	e := NewWithError(10086, errors.New("aaa"))
	b := Marshal(e)
	var re BadResponse
	json.Unmarshal(b, &re)
	assert.Equal(t, re.Code, 10086)
}

func TestConvertBadResponse(t *testing.T) {
	err := ConvertBadResponse([]byte("bad"))
	assert.NotNil(t, err)
}