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

import "net"

// GetLocalIP -
// try to use interfaces addresses
// if fail, fall back to 127.0.0.1
func GetLocalIP() string {
	fallbackChoice := "127.0.0.1"

	// try to use interfaces addresses
	interfaces, err := net.Interfaces()
	if err != nil {
		return fallbackChoice
	}
	for _, iface := range interfaces {
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			// Check if the address is an IP address
			ipnet, ok := addr.(*net.IPNet)
			if !ok || ipnet.IP.IsLoopback() {
				continue
			}
			if ipnet.IP.To4() == nil {
				continue
			}
			if "127.0.0.1" == ipnet.IP.String() || "172.17.0.1" == ipnet.IP.String() {
				continue
			}
			return ipnet.IP.String()
		}
	}
	return fallbackChoice
}
