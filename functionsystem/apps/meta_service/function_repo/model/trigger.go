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

package model

import (
	"encoding/json"
	"time"

	"meta_service/function_repo/errmsg"
)

const (
	// HTTPType is http trigger
	HTTPType = "HTTP"
)

// TriggerInfo is a reply of create request and update request
type TriggerInfo struct {
	TriggerID   string          `json:"triggerId"`
	FuncID      string          `json:"funcId"`
	TriggerType string          `json:"triggerType"`
	RevisionID  string          `json:"revisionId"`
	CreateTime  time.Time       `json:"createTime"`
	UpdateTime  time.Time       `json:"updateTime"`
	RawSpec     json.RawMessage `json:"spec"`
	Spec        interface{}     `json:"-"`
}

// localTriggerCreateRequest prevents recursive call to MarshalJSON and UnmarshalJSON
type localTriggerInfo TriggerInfo

// MarshalJSON implements json.Marshaler
func (t TriggerInfo) MarshalJSON() ([]byte, error) {
	l := (*localTriggerInfo)(&t)
	return triggerMarshalSpec(l, l.Spec, &l.RawSpec)
}

// UnmarshalJSON implements json.Unmarshaler
func (t *TriggerInfo) UnmarshalJSON(b []byte) error {
	l := (*localTriggerInfo)(t)
	return triggerUnmarshalSpec(b, l, &l.Spec, &l.TriggerType, &l.RawSpec)
}

// TriggerSpec contains common fields for every specific trigger specs
type TriggerSpec struct {
	FuncID      string `json:"funcId"`
	TriggerID   string `json:"triggerId"`
	TriggerType string `json:"triggerType"`
}

// HTTPTriggerSpec is a request of http trigger.
type HTTPTriggerSpec struct {
	TriggerSpec
	HTTPMethod    string `json:"httpMethod"`
	ResourceID    string `json:"resourceId"`
	AuthFlag      bool   `json:"authFlag"`
	AuthAlgorithm string `json:"authAlgorithm"`
	TriggerURL    string `json:"triggerUrl"`
	AppID         string `json:"appId"`
	AppSecret     string `json:"appSecret"`
}

// TriggerCreateRequest is the request body when creating a trigger.
type TriggerCreateRequest struct {
	FuncID      string          `json:"funcId" valid:"maxstringlength(255)"`
	TriggerType string          `json:"triggerType" valid:"required"`
	RawSpec     json.RawMessage `json:"spec"`
	Spec        interface{}     `json:"-"`
}

// localTriggerCreateRequest prevents recursive call to MarshalJSON and UnmarshalJSON
type localTriggerCreateRequest TriggerCreateRequest

// MarshalJSON implements json.Marshaler
func (t TriggerCreateRequest) MarshalJSON() ([]byte, error) {
	l := (*localTriggerCreateRequest)(&t)
	return triggerMarshalSpec(l, l.Spec, &l.RawSpec)
}

// UnmarshalJSON implements json.Unmarshaler
func (t *TriggerCreateRequest) UnmarshalJSON(b []byte) error {
	l := (*localTriggerCreateRequest)(t)
	return triggerUnmarshalSpec(b, l, &l.Spec, &l.TriggerType, &l.RawSpec)
}

// TriggerResponse is the response when creating ,getting or updating a trigger.
type TriggerResponse struct {
	TriggerInfo
}

// TriggerListGetResponse is the response when getting triggers.
type TriggerListGetResponse struct {
	Count       int           `json:"total"`
	TriggerList []TriggerInfo `json:"triggers"`
}

// TriggerUpdateRequest is the request body when updating a trigger.
type TriggerUpdateRequest struct {
	TriggerID   string          `json:"triggerId" valid:"maxstringlength(64)"`
	RevisionID  string          `json:"revisionId"`
	TriggerMode string          `json:"triggerMode"`
	TriggerType string          `json:"triggerType"`
	RawSpec     json.RawMessage `json:"spec"`
	Spec        interface{}     `json:"-"`
}

// localTriggerUpdateRequest prevents recursive call to MarshalJSON and UnmarshalJSON
type localTriggerUpdateRequest TriggerUpdateRequest

// MarshalJSON implements json.Marshaler
func (t TriggerUpdateRequest) MarshalJSON() ([]byte, error) {
	l := (*localTriggerUpdateRequest)(&t)
	return triggerMarshalSpec(l, l.Spec, &l.RawSpec)
}

// UnmarshalJSON implements json.Unmarshaler
func (t *TriggerUpdateRequest) UnmarshalJSON(b []byte) error {
	l := (*localTriggerUpdateRequest)(t)
	return triggerUnmarshalSpec(b, l, &l.Spec, &l.TriggerType, &l.RawSpec)
}

func triggerMarshalSpec(v, spec interface{}, raw *json.RawMessage) ([]byte, error) {
	b, err := json.Marshal(spec)
	if err != nil {
		return nil, err
	}
	*raw = json.RawMessage(b)
	return json.Marshal(v)
}

func triggerUnmarshalSpec(b []byte, v interface{}, spec *interface{}, typ *string, raw *json.RawMessage) error {
	if err := json.Unmarshal(b, v); err != nil {
		return err
	}

	switch *typ {
	case HTTPType:
		*spec = &HTTPTriggerSpec{}
	default:
		return errmsg.NewParamError("invalid triggertype: %s", *typ)
	}
	if err := json.Unmarshal(*raw, spec); err != nil {
		return errmsg.NewParamError(err.Error())
	}
	return nil
}
