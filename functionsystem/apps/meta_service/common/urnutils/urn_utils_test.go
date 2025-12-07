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

package urnutils

import (
	"reflect"
	"testing"

	"meta_service/common/functioncapability"

	"github.com/stretchr/testify/assert"
)

func TestProductUrn_ParseFrom(t *testing.T) {
	absURN := BaseURN{
		"absPrefix",
		"absZone",
		"absBusinessID",
		"absTenantID",
		"absProductID",
		"absName",
		"latest",
	}
	absURNStr := "absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:latest"
	type args struct {
		urn string
	}
	tests := []struct {
		name   string
		fields BaseURN
		args   args
		want   BaseURN
	}{
		{
			name: "normal test",
			args: args{
				absURNStr,
			},
			want: absURN,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			p := &BaseURN{}
			if _ = p.ParseFrom(tt.args.urn); !reflect.DeepEqual(*p, tt.want) {
				t.Errorf("ParseFrom() p = %v, want %v", *p, tt.want)
			}
		})
	}
}

func TestProductUrn_String(t *testing.T) {
	tests := []struct {
		name   string
		fields BaseURN
		want   string
	}{
		{
			"stringify with version",
			BaseURN{
				"absPrefix",
				"absZone",
				"absBusinessID",
				"absTenantID",
				"absProductID",
				"absName",
				"latest",
			},
			"absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:latest",
		},
		{
			"stringify without version",
			BaseURN{
				ProductID:  "absPrefix",
				RegionID:   "absZone",
				BusinessID: "absBusinessID",
				TenantID:   "absTenantID",
				TypeSign:   "absProductID",
				Name:       "absName",
			},
			"absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			p := &BaseURN{
				ProductID:  tt.fields.ProductID,
				RegionID:   tt.fields.RegionID,
				BusinessID: tt.fields.BusinessID,
				TenantID:   tt.fields.TenantID,
				TypeSign:   tt.fields.TypeSign,
				Name:       tt.fields.Name,
				Version:    tt.fields.Version,
			}
			if got := p.String(); got != tt.want {
				t.Errorf("String() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestProductUrn_StringWithoutVersion(t *testing.T) {
	tests := []struct {
		name   string
		fields BaseURN
		want   string
	}{
		{
			"stringify without version",
			BaseURN{
				"absPrefix",
				"absZone",
				"absBusinessID",
				"absTenantID",
				"absProductID",
				"absName",
				"latest",
			},
			"absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			p := &BaseURN{
				ProductID:  tt.fields.ProductID,
				RegionID:   tt.fields.RegionID,
				BusinessID: tt.fields.BusinessID,
				TenantID:   tt.fields.TenantID,
				TypeSign:   tt.fields.TypeSign,
				Name:       tt.fields.Name,
				Version:    tt.fields.Version,
			}
			if got := p.StringWithoutVersion(); got != tt.want {
				t.Errorf("StringWithoutVersion() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestAnonymize(t *testing.T) {
	tests := []struct {
		input string
		want  string
	}{
		{"0", anonymization},
		{"123", anonymization},
		{"123456", anonymization},
		{"1234567", "123****567"},
		{"12345678901234546", "123****546"},
	}
	for _, tt := range tests {
		assert.Equal(t, tt.want, Anonymize(tt.input))
	}
}

func TestAnonymizeTenantURN(t *testing.T) {
	tests := []struct {
		input string
		want  string
	}{
		{"absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName", "absPrefix:absZone:absBusinessID:abs****tID:absProductID:absName"},
		{"absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:latest", "absPrefix:absZone:absBusinessID:abs****tID:absProductID:absName:latest"},
		{"a:b:c", "a:b:c"},
	}
	for _, tt := range tests {
		assert.Equal(t, tt.want, AnonymizeTenantURN(tt.input))
	}
}

func TestBaseURN_Valid(t *testing.T) {
	Separator = "@"
	functionCapability := functioncapability.Fusion
	urn := BaseURN{
		ProductID:  "",
		RegionID:   "",
		BusinessID: "",
		TenantID:   "",
		TypeSign:   "",
		Name:       "0@a_-9AA@AA",
		Version:    "",
	}
	success := urn.Valid(functionCapability)
	assert.Equal(t, nil, success)

	urn.Name = "0@a_-9AA@ttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt"
	success = urn.Valid(functionCapability)
	assert.Equal(t, nil, success)

	urn.Name = "0@a_-9AA@tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt"
	err := urn.Valid(functionCapability)
	assert.NotEqual(t, nil, err)

	urn.Name = "@func"
	err = urn.Valid(functionCapability)
	assert.NotEqual(t, nil, err)

	urn.Name = "0@func"
	err = urn.Valid(functionCapability)
	assert.NotEqual(t, nil, err)

	urn.Name = "0@^@^"
	err = urn.Valid(functionCapability)
	assert.NotEqual(t, nil, err)

	Separator = "-"
}

func TestBaseURN_GetAlias(t *testing.T) {
	urn := BaseURN{
		ProductID:  "",
		RegionID:   "",
		BusinessID: "",
		TenantID:   "",
		TypeSign:   "",
		Name:       "0@a_-9AA@AA",
		Version:    DefaultURNVersion,
	}

	alias := urn.GetAlias()
	assert.Equal(t, "", alias)

	urn.Version = "old"
	alias = urn.GetAlias()
	assert.Equal(t, "old", alias)
}

func TestGetFuncInfoWithVersion(t *testing.T) {
	urn := "urn"
	_, err := GetFuncInfoWithVersion(urn)
	assert.NotEqual(t, nil, err)

	urn = "absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName"
	_, err = GetFuncInfoWithVersion(urn)
	assert.NotEqual(t, nil, err)

	urn = "absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:latest"
	parsedURN, err := GetFuncInfoWithVersion(urn)
	assert.Equal(t, "absName", parsedURN.Name)
}

func TestAnonymizeTenantKey(t *testing.T) {
	inputKey := ""
	outputKey := AnonymizeTenantKey(inputKey)
	assert.Equal(t, "****", outputKey)

	inputKey = "input/key"
	outputKey = AnonymizeTenantKey(inputKey)
	assert.Equal(t, "****/key", outputKey)
}

func TestParseAliasURN(t *testing.T) {
	urn := ""
	alias := ParseAliasURN(urn)
	assert.Equal(t, urn, alias)

	urn = "absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:!latest"
	alias = ParseAliasURN(urn)
	assert.Equal(t, "absPrefix:absZone:absBusinessID:absTenantID:absProductID:absName:latest", alias)
}

func TestAnonymizeTenantURNSlice(t *testing.T) {
	inUrn := []string{"in", "in/urn"}
	outUrn := AnonymizeTenantURNSlice(inUrn)
	assert.Equal(t, "in", outUrn[0])
	assert.Equal(t, "in/urn", outUrn[1])
}

func TestBaseURN_GetAliasForFuncBranch(t *testing.T) {
	urn := BaseURN{
		ProductID:  "",
		RegionID:   "",
		BusinessID: "",
		TenantID:   "",
		TypeSign:   "",
		Name:       "0@a_-9AA@AA",
		Version:    "!latest",
	}

	alias := urn.GetAliasForFuncBranch()
	assert.Equal(t, "latest", alias)

	urn.Version = "latest"
	alias = urn.GetAliasForFuncBranch()
	assert.Equal(t, "", alias)
}
