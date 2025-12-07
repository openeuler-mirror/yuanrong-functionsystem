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

// Package timeutil provides some utils for time
package timeutil

import "time"

const (
	// NanosecondToMillisecond is the factor to convert nanosecond to millisecond,
	// it should be equal to time.Millisecond/time.Nanosecond
	NanosecondToMillisecond = 1000000
)

// UnixMillisecond convert time.Time to unix timestamp in millisecond
func UnixMillisecond(t time.Time) int64 {
	return t.UnixNano() / NanosecondToMillisecond
}

// NowUnixMillisecond get current unix timestamp in millisecond
func NowUnixMillisecond() int64 {
	return UnixMillisecond(time.Now())
}

// NowUnixNanoseconds get current unix timestamp in nanoseconds
func NowUnixNanoseconds() int64 {
	return time.Now().UnixNano()
}
