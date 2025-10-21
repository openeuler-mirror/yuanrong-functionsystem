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
	"io/ioutil"
	"reflect"
	"sort"
	"strings"
)

const pairLen = 2

// MasterInfo is the master info string in a struct
type MasterInfo struct {
	MasterIP            string   `key:"master_ip"`
	EtcdIP              string   `key:"etcd_ip"`
	EtcdPort            string   `key:"etcd_port"`
	GlobalSchedulerPort string   `key:"global_scheduler_port"`
	DsMasterPort        string   `key:"ds_master_port"`
	EtcdPeerPort        string   `key:"etcd_peer_port"`
	BusProxyPort        string   `key:"bus-proxy"`
	BusPort             string   `key:"bus"`
	DsWorkerPort        string   `key:"ds-worker"`
	LocalIP             string   `key:"local_ip"`
	EtcdAddrList        []string `key:"etcd_addr_list"`
}

// GetMasterInfoString returns the master info string
func GetMasterInfoString(filename string) (string, error) {
	content, err := ioutil.ReadFile(filename)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(content)), nil
}

// ExtractMasterInfo parses the input content string into a MasterInfo struct.
func ExtractMasterInfo(content string) *MasterInfo {
	masterInfo := &MasterInfo{}
	keyValues := strings.Split(content, ",")

	for _, kv := range keyValues {
		pair := strings.Split(kv, ":")
		if len(pair) != pairLen {
			continue
		}
		key := strings.TrimSpace(pair[0])
		value := strings.TrimSpace(pair[1])

		setFieldByKey(masterInfo, key, value)
	}
	return masterInfo
}

// setFieldByKey sets the field in MasterInfo based on the key tag.
func setFieldByKey(masterInfo *MasterInfo, key, value string) {
	masterInfoValue := reflect.ValueOf(masterInfo).Elem()
	masterInfoType := masterInfoValue.Type()

	for i := 0; i < masterInfoType.NumField(); i++ {
		field := masterInfoType.Field(i)
		tag := field.Tag.Get("key")
		if tag == key {
			fieldValue := masterInfoValue.Field(i)
			if fieldValue.Kind() == reflect.Slice {
				fieldValue.Set(reflect.Append(fieldValue, reflect.ValueOf(value)))
			} else {
				fieldValue.SetString(value)
			}
			break
		}
	}
}

// GetMasterInfoFromFile returns a master info in a struct
func GetMasterInfoFromFile(filename string) (*MasterInfo, error) {
	content, err := GetMasterInfoString(filename)
	if err != nil {
		return nil, err
	}
	return ExtractMasterInfo(content), nil
}

// IsMasterNode check if the master info belongs to a master node, which is started by "yr start --master ..." command
func (mi *MasterInfo) IsMasterNode() bool {
	if len(mi.EtcdAddrList) > 0 {
		for _, e := range mi.EtcdAddrList {
			if mi.LocalIP == e {
				return true
			}
		}
		return false
	}
	if mi.MasterIP == mi.LocalIP {
		return true
	}
	return false
}

// IsSameCluster check if the incoming master info belongs to the same cluster of self
func (mi *MasterInfo) IsSameCluster(incomingMi *MasterInfo) bool {
	if len(mi.EtcdAddrList) != len(incomingMi.EtcdAddrList) {
		return false
	}
	if len(mi.EtcdAddrList) == 0 {
		if mi.MasterIP == incomingMi.MasterIP && mi.EtcdIP == incomingMi.EtcdIP && mi.EtcdPort == incomingMi.EtcdPort {
			return true
		}
		return false
	}
	sort.Sort(sort.StringSlice(mi.EtcdAddrList))
	sort.Sort(sort.StringSlice(incomingMi.EtcdAddrList))
	for i, v := range mi.EtcdAddrList {
		if incomingMi.EtcdAddrList[i] != v {
			return false
		}
	}
	return true
}
