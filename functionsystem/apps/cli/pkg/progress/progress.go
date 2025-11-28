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

// Package progress use to displays work progress.
package progress

import (
	"fmt"
	"os"
	"strings"
	"time"
)

const (
	defaultRepeatCount = 9
	defaultSleepTime   = 300
	// FailedDone Failed to done
	FailedDone = "fail"
)

// Progress process info
type Progress struct {
	Begin        time.Time
	DoingMessage string
	RepeatCount  int
	DoneChan     chan struct{}
}

// CreateAndDoing crate a progress and start doing
func CreateAndDoing(doingMessage string) *Progress {
	progress := &Progress{
		Begin:        time.Now(),
		RepeatCount:  defaultRepeatCount,
		DoingMessage: doingMessage,
		DoneChan:     make(chan struct{}),
	}
	go progress.doing()
	return progress
}

// Doing print doing message until done
func (p *Progress) doing() {
	for i := 1; ; i++ {
		select {
		case _, ok := <-p.DoneChan:
			if !ok {
				// 通道关闭则退出
				return
			}
		default:
			dynamicMessage := strings.Repeat(".", i)
			if i == p.RepeatCount {
				dynamicMessage = strings.Repeat(" ", i)
				i = 0
			}
			if _, err := fmt.Fprintf(os.Stdout, "%s %s\r", p.DoingMessage, dynamicMessage); err != nil {
				continue
			}
			time.Sleep(time.Millisecond * defaultSleepTime)
		}
	}
}

// Done set Progress done
func (p *Progress) Done(doneMessage string) {
	if doneMessage != FailedDone {
		elapsed := time.Since(p.Begin)
		fmt.Printf("%s costs time %dms\n", doneMessage, elapsed.Milliseconds())
	}
	close(p.DoneChan)
}
