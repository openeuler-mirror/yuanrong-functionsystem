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

// Package types -
package types

import (
	"context"
	"sync/atomic"
)

// TCPRequest contains users request
type TCPRequest struct {
	LogType          string
	RawData          []byte
	RequestID        uint64
	UserData         map[string]string
	Priority         int
	DirectRequest    bool
	CanBackpressure  bool
	CPU              string
	Memory           string
	InvokeType       string
	AffinityInfo     AffinityInfo
	GroupInfo        GroupInfo
	ResourceMetaData map[string]float32
	TraceCtx         context.Context
}

// TCPResponse contains users response
type TCPResponse struct {
	ErrorCode    uint32
	ErrorMessage string
	RawData      []byte
	Logs         string
	RequestID    uint64
	Summary      string
}

// Position position where to do requests
type Position struct {
	InstanceID string
	NodeID     string
}

// DispatcherRequest call request struct
type DispatcherRequest struct {
	Request     *TCPRequest
	Response    chan *TCPResponse
	Position    Position
	FunctionKey string
	TraceID     string
	NodeLabel   string
	FutureID    string
	canceled    uint32
	WorkerID    string
}

// Cancel set status of request to canceled
func (dr *DispatcherRequest) Cancel() {
	atomic.StoreUint32(&dr.canceled, 1)
}

// IsCanceled query whether request is canceled
func (dr *DispatcherRequest) IsCanceled() bool {
	return atomic.LoadUint32(&dr.canceled) == 1
}
