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

// Package test is used for unit tests
package test

import (
	"bytes"
	"io/ioutil"

	"cli/pkg/cmdio"
)

// CmdIO mock io of cmd
type CmdIO struct {
	In     *bytes.Buffer
	Out    *bytes.Buffer
	ErrOut *bytes.Buffer
}

// MockCmdIO mock cmdio for unit tests
func MockCmdIO() (*cmdio.CmdIO, *CmdIO) {
	in := &bytes.Buffer{}
	out := &bytes.Buffer{}
	errOut := &bytes.Buffer{}
	cio := &cmdio.CmdIO{
		In:     ioutil.NopCloser(in),
		Out:    out,
		ErrOut: errOut,
	}
	tio := &CmdIO{
		In:     in,
		Out:    out,
		ErrOut: errOut,
	}
	return cio, tio
}
