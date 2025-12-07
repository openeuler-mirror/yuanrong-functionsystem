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

package codec

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestReflectCodec(t *testing.T) {
	codec := NewReflectCodec("prefix", true)
	type Student struct {
		Name   string
		Age    int
		Gender bool
	}

	s, err := codec.Encode(Student{Name: "Alice", Age: 15})
	require.NoError(t, err)
	assert.Equal(t, "prefix/Alice/c15/0/", s)

	var alice Student
	err = codec.Decode(s, &alice)
	require.NoError(t, err)
	assert.Equal(t, "Alice", alice.Name)
	assert.Equal(t, 15, alice.Age)
	assert.Equal(t, false, alice.Gender)
}

func TestReflectCodecTrim(t *testing.T) {
	codec := NewReflectCodec("prefix", true)
	type Student struct {
		Name  string
		Age   int
		Phone string
	}

	{
		s, err := codec.Encode(Student{Name: "Alice", Age: 15})
		require.NoError(t, err)
		assert.Equal(t, "prefix/Alice/c15/", s)

		var alice Student
		err = codec.Decode(s, &alice)
		require.NoError(t, err)
		assert.Equal(t, "Alice", alice.Name)
		assert.Equal(t, 15, alice.Age)
		assert.Equal(t, "", alice.Phone)
	}
	{
		s, err := codec.Encode(Student{Name: "Alice"})
		require.NoError(t, err)
		assert.Equal(t, "prefix/Alice/", s)

		var alice Student
		err = codec.Decode(s, &alice)
		require.NoError(t, err)
		assert.Equal(t, "Alice", alice.Name)
		assert.Equal(t, 0, alice.Age)
		assert.Equal(t, "", alice.Phone)
	}
}

func TestReflectCodecNil(t *testing.T) {
	codec := NewReflectCodec("prefix", true)
	type Student struct {
		Name string
		Age  int
	}

	s, err := codec.Encode(Student{Age: 15})
	require.NoError(t, err)
	assert.Equal(t, "prefix//c15/", s)

	var alice Student
	err = codec.Decode(s, &alice)
	require.NoError(t, err)
	assert.Equal(t, "", alice.Name)
	assert.Equal(t, 15, alice.Age)
}

func TestReflectCodecNested(t *testing.T) {
	codec := NewReflectCodec("prefix", true)
	type Address struct {
		Street string
		Code   uint
	}
	type Student struct {
		Name    string
		Age     int
		Address Address
	}

	{
		s, err := codec.Encode(Student{Name: "Alice", Age: 15, Address: Address{Street: "abc road", Code: 12345}})
		require.NoError(t, err)
		assert.Equal(t, "prefix/Alice/c15/abc road/f12345/", s)

		var alice Student
		err = codec.Decode(s, &alice)
		require.NoError(t, err)
		assert.Equal(t, "Alice", alice.Name)
		assert.Equal(t, 15, alice.Age)
		assert.Equal(t, "abc road", alice.Address.Street)
		assert.Equal(t, uint(12345), alice.Address.Code)
	}
	{
		s, err := codec.Encode(Student{Name: "Alice", Address: Address{Street: "abc road", Code: 12345}})
		require.NoError(t, err)
		assert.Equal(t, "prefix/Alice//abc road/f12345/", s)

		var alice Student
		err = codec.Decode(s, &alice)
		require.NoError(t, err)
		assert.Equal(t, "Alice", alice.Name)
		assert.Equal(t, 0, alice.Age)
		assert.Equal(t, "abc road", alice.Address.Street)
		assert.Equal(t, uint(12345), alice.Address.Code)
	}
	{
		s, err := codec.Encode(Student{Name: "Alice", Address: Address{Street: "abc road"}})
		require.NoError(t, err)
		assert.Equal(t, "prefix/Alice//abc road/", s)

		var alice Student
		err = codec.Decode(s, &alice)
		require.NoError(t, err)
		assert.Equal(t, "Alice", alice.Name)
		assert.Equal(t, 0, alice.Age)
		assert.Equal(t, "abc road", alice.Address.Street)
	}
}
