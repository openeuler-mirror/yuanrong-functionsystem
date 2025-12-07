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

	"github.com/smartystreets/goconvey/convey"
	"github.com/stretchr/testify/assert"
)

func TestComplexFuncName_GetSvcIDWithPrefix(t *testing.T) {
	tests := []struct {
		name   string
		fields ComplexFuncName
		want   string
	}{
		{
			name: "normal",
			fields: ComplexFuncName{
				prefix:    ServiceIDPrefix,
				ServiceID: "absserviceid",
				FuncName:  "absFuncName",
			},
			want: "0-absserviceid",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			c := &ComplexFuncName{
				prefix:    tt.fields.prefix,
				ServiceID: tt.fields.ServiceID,
				FuncName:  tt.fields.FuncName,
			}
			if got := c.GetSvcIDWithPrefix(); got != tt.want {
				t.Errorf("GetSvcIDWithPrefix() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestComplexFuncName_ParseFrom(t *testing.T) {
	type args struct {
		name string
	}
	tests := []struct {
		name string
		args args
		want *ComplexFuncName
	}{
		{
			name: "normal",
			args: args{
				name: "0:absserviceid:absFunc:Name",
			},
			want: &ComplexFuncName{
				prefix:    ServiceIDPrefix,
				ServiceID: "absserviceid",
				FuncName:  "absFunc:Name",
			},
		},
	}
	SetSeparator(":")
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			c := &ComplexFuncName{}
			if got := c.ParseFrom(tt.args.name); !reflect.DeepEqual(got, tt.want) {
				t.Errorf("ParseFrom() = %v, want %v", got, tt.want)
			}
		})
	}
	SetSeparator("-")

	pName := "1:serviceId:absFunc:Name"
	cName := &ComplexFuncName{}
	cName.ParseFrom(pName)
	assert.Equal(t, pName, cName.FuncName)
}

func TestComplexFuncName_String(t *testing.T) {
	tests := []struct {
		name   string
		fields ComplexFuncName
		want   string
	}{
		{
			name: "normal",
			fields: ComplexFuncName{
				prefix:    ServiceIDPrefix,
				ServiceID: "absserviceid",
				FuncName:  "absFunc-Name",
			},
			want: "0-absserviceid-absFunc-Name",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			c := &ComplexFuncName{
				prefix:    tt.fields.prefix,
				ServiceID: tt.fields.ServiceID,
				FuncName:  tt.fields.FuncName,
			}
			if got := c.String(); got != tt.want {
				t.Errorf("String() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestNewComplexFuncName(t *testing.T) {
	type args struct {
		svcID    string
		funcName string
	}
	tests := []struct {
		name string
		args args
		want *ComplexFuncName
	}{
		{
			name: "normal",
			args: args{
				svcID:    "absserviceid",
				funcName: "absFunc-Name",
			},
			want: &ComplexFuncName{
				prefix:    ServiceIDPrefix,
				ServiceID: "absserviceid",
				FuncName:  "absFunc-Name",
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := NewComplexFuncName(tt.args.svcID, tt.args.funcName); !reflect.DeepEqual(got, tt.want) {
				t.Errorf("NewComplexFuncName() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestGetPureFaaSFunctionName(t *testing.T) {
	convey.Convey("test GetPureFaaSFunctionName", t, func() {
		funcName := GetPureFaaSFunctionName("0@yrservice@yrfunc")
		convey.So(funcName, convey.ShouldEqual, "yrfunc")
	})
}

func TestGetPureFaaSService(t *testing.T) {
	convey.Convey("test GetPureFaaSService", t, func() {
		funcSvc := GetPureFaaSService("0@yrservice@yrfunc")
		convey.So(funcSvc, convey.ShouldEqual, "yrservice")
	})
}

func TestComplexFaaSFuncName(t *testing.T) {
	convey.Convey("test ComplexFaaSFuncName", t, func() {
		funcName := ComplexFaaSFuncName("yrservice", "yrfunc")
		convey.So(funcName, convey.ShouldEqual, "0@yrservice@yrfunc")
	})
}

func TestGetFunctionVersion(t *testing.T) {
	convey.Convey("test GetFunctionVersion", t, func() {
		version := GetFunctionVersion("faas")
		convey.So(version, convey.ShouldEqual, "latest")
		version = GetFunctionVersion("yr-lib")
		convey.So(version, convey.ShouldEqual, "$latest")
	})
}
