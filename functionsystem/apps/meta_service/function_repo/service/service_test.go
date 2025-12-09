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

package service

import (
	"fmt"
	"os"
	"testing"

	"github.com/stretchr/testify/assert"

	"meta_service/common/urnutils"
	"meta_service/function_repo/initialize"
	"meta_service/function_repo/model"
	"meta_service/function_repo/server"
	"meta_service/function_repo/test"
	"meta_service/function_repo/test/fakecontext"
)

func TestBuildNewServicePreix(t *testing.T) {
	id := "hello"
	res := buildNewServicePrefix(id, "")
	str := urnutils.ServicePrefix + id + urnutils.DefaultSeparator
	if !(res == str) {
		t.Errorf("get newServicePreix failed, res is %s", res)
	}
	id = ""
	res = buildNewServicePrefix(id, "")
	if !(res == urnutils.ServicePrefix) {
		t.Errorf("get newServicePreix failed, res is %s", res)
	}
}

// TestMain init router test
func TestMain(m *testing.M) {
	if !initialize.Initialize(test.ConfigPath, "/home/sn/config/log.json") {
		fmt.Println("failed to initialize")
		os.Exit(1)
	}
	test.ResetETCD()
	result := m.Run()
	test.ResetETCD()
	os.Exit(result)
}

func TestGetServiceID(t *testing.T) {
	type args struct {
		ctx server.Context
	}

	tests := []struct {
		name    string
		args    args
		want    model.FunctionVersion
		wantErr bool
	}{
		{"test get service id",
			args{fakecontext.NewMockContext()}, model.FunctionVersion{}, false},
	}
	tt := tests[0]
	t.Run(tt.name, func(t *testing.T) {
		_, err := GetServiceID(tt.args.ctx, "func-id", "")
		assert.Equal(t, err, nil)
	})
}
