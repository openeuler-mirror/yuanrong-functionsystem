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

package signals

import (
	"syscall"
	"testing"
	"time"

	"gotest.tools/assert"
)

func TestWaitForSignal(t *testing.T) {
	stopCh := WaitForSignal()

	go func() {
		time.Sleep(200 * time.Millisecond)
		shutdownHandler <- syscall.SIGTERM
	}()
	select {
	case <-stopCh:
		t.Log("received termination signal")
	case <-time.After(time.Second):
		t.Fatal("failed to signal in 1s")
	}

	_, ok := <-stopCh
	assert.Equal(t, ok, false)
}
