/*
 * Copyright (c) 2022 Huawei Technologies Co., Ltd
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

// Package timer delayed execution function
package timer

import (
	"container/heap"
	"errors"
	"reflect"
	"sync"
	"time"

	"meta_service/common/logger/log"
)

type timerInfo struct {
	function interface{}
	params   []interface{}
	target   int64
}

// FuncHeap delay function information top heap
var FuncHeap *delayFuncHeap

type delayFuncHeap []timerInfo

var once sync.Once

// InitTimer Creating a global timer
func InitTimer() {
	once.Do(func() {
		FuncHeap = &delayFuncHeap{}
		heap.Init(FuncHeap)
		go FuncHeap.startTimer()
	})
}

func (d *delayFuncHeap) startTimer() {
	timer := time.NewTicker(time.Second)
	for {
		<-timer.C
		for len(*d) > 0 {
			x := (*d)[0]
			if x.target > time.Now().Unix() {
				break
			}
			funcInfoIf := heap.Pop(d)
			if funcInfoIf == nil {
				log.GetLogger().Warn("function info is nil.")
				continue
			}
			funcInfo, ok := funcInfoIf.(timerInfo)
			if !ok {
				log.GetLogger().Warn("failed to executing function ,err: type error")
				continue
			}
			_, err := call(funcInfo.function, funcInfo.params...)
			if err != nil {
				log.GetLogger().Warnf("failed to executing function ,err: %s", err.Error())
			}
		}
	}
}

// AddFunc add delay function
func (d *delayFuncHeap) AddFunc(delayTime int, function interface{}, params ...interface{}) {
	heap.Push(d, timerInfo{
		function: function,
		params:   params,
		target:   time.Now().Unix() + int64(delayTime),
	})
}

func call(function interface{}, params ...interface{}) ([]reflect.Value, error) {
	if function == nil {
		return nil, errors.New("function is nil")
	}
	defer func() {
		if err := recover(); err != nil {
			log.GetLogger().Errorf("function call failed, err: %s", err)
		}
	}()
	functionValueOf := reflect.ValueOf(function)
	if len(params) != functionValueOf.Type().NumIn() {
		return nil, errors.New("the number of params is not adapted")
	}

	in := make([]reflect.Value, len(params))
	for k, param := range params {
		in[k] = reflect.ValueOf(param)
	}
	result := functionValueOf.Call(in)
	return result, nil
}

// Len returns the size
func (d delayFuncHeap) Len() int {
	return len(d)
}

// Less is used to compare two objects in the heap.
func (d delayFuncHeap) Less(i, j int) bool {
	if i >= 0 && j >= 0 && i < d.Len() && j < d.Len() {
		return d[i].target < d[j].target
	}
	log.GetLogger().Errorf("Index out of bound")
	return false
}

// Swap implements swapping of two elements in the heap.
func (d delayFuncHeap) Swap(i, j int) {
	if i >= 0 && j >= 0 && i < d.Len() && j < d.Len() {
		d[i], d[j] = d[j], d[i]
	} else {
		log.GetLogger().Errorf("Index out of bound")
	}
}

// Push push an item to heap
func (d *delayFuncHeap) Push(x interface{}) {
	*d = append(*d, x.(timerInfo))
}

// Pop pop an item from heap
func (d *delayFuncHeap) Pop() interface{} {
	old := *d
	n := len(old)
	x := old[n-1]
	*d = old[0 : n-1]
	return x
}
