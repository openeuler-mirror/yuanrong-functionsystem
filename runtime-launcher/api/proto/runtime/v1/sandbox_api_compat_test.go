// Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package runtimev1

import (
	"testing"

	"google.golang.org/protobuf/proto"
)

func TestStartResponseSandboxdWireCompatibility(t *testing.T) {
	// sandboxd encodes message at field 2, id at field 3, and sandbox_ip at
	// field 4. Use fixed wire bytes so a local schema change cannot alter the fixture.
	const base = "\x12\x07Succeed\x1a\x07sandbox"
	for _, tc := range []struct {
		name string
		wire string
		ip   string
	}{
		{name: "sandbox_ip", wire: base + "\x22\x0a192.0.2.10", ip: "192.0.2.10"},
		{name: "omitted_sandbox_ip", wire: base},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var response StartResponse
			if err := proto.Unmarshal([]byte(tc.wire), &response); err != nil {
				t.Fatal(err)
			}
			if response.GetCode() != 0 || response.GetMessage() != "Succeed" || response.GetId() != "sandbox" {
				t.Fatalf("unexpected start result: %v", &response)
			}
			if response.GetSandboxIp() != tc.ip {
				t.Fatalf("sandbox_ip = %q, want %q", response.GetSandboxIp(), tc.ip)
			}
		})
	}
}
