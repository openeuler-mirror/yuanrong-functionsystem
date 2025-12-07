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

// Package logwrap print logs by frequency.
package logwrap

import "testing"

func TestLogWrap(t *testing.T) {
	Store("runtime1")
	Print("runtime1", "", "TestLogWrap: %s")
	Print("runtime1", INFO, "TestLogWrap: %s")
	Print("runtime1", ERROR, "TestLogWrap: %s")
	Print("runtime1", DEBUG, "TestLogWrap: %s")
	Print("runtime1", WARN, "TestLogWrap: %s")
	Delete("runtime1")
}