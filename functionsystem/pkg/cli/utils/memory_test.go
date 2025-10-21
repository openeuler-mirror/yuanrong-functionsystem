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

package utils

import (
	"io"
	"os"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/smartystreets/goconvey/convey"
)

func TestReadMemoryStats(t *testing.T) {
	memInfoContent := `MemTotal:       16384 kB
MemFree:        8192 kB
MemAvailable:   8192 kB
`

	tmpFile, err := os.CreateTemp("", "meminfo_test")
	if err != nil {
		t.Fatal(err)
	}
	defer tmpFile.Close()
	_, err = tmpFile.WriteString(memInfoContent)
	if err != nil {
		t.Fatal(err)
	}

	patches := gomonkey.ApplyFunc(os.Open, func(name string) (*os.File, error) {
		if name == "/proc/meminfo" {
			tmpFile.Seek(0, io.SeekStart)
			return tmpFile, nil
		}
		return os.Open(name)
	})
	defer patches.Reset()

	convey.Convey("Test ReadMemoryStats", t, func() {
		memStats, err := ReadMemoryStats()

		convey.So(err, convey.ShouldBeNil)
		convey.So(memStats.MemTotal, convey.ShouldEqual, 16384) // 16384 KB = 16 MB
		convey.So(memStats.MemFree, convey.ShouldEqual, 8192)   // 8192 KB = 8 MB
	})
}
