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

package version

import (
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"cli/constant"
	"cli/pkg/test"
	"cli/utils/colorprint"
)

func TestInitVersionCMD(t *testing.T) {
	testCases := []struct {
		name      string
		version   string
		buildTime string
		wantStd   string
	}{
		{
			name:      "get version info",
			version:   `1.0.0`,
			buildTime: `20210218`,
			wantStd: "CLI version: " +
				colorprint.GetPrintString("1.0.0", colorprint.KeywordColor) + ".\n" +
				"Using yuanrong at: " + constant.YuanRongInstallationDir + "\n",
		},
	}
	for _, tt := range testCases {
		t.Run(tt.name, func(t *testing.T) {
			p := gomonkey.ApplyGlobalVar(&constant.Version, tt.version)
			p.ApplyGlobalVar(&constant.Date, tt.buildTime)
			defer p.Reset()

			cio, mio := test.MockCmdIO()
			cmd := InitVersionCMD(cio)

			cmd.SetArgs([]string{})
			_, err := cmd.ExecuteC()
			require.NoError(t, err)
			assert.Equal(t, tt.wantStd, mio.Out.String())
		})
	}
}
