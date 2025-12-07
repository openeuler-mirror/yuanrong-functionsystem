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

// Package utils for common functions
package utils

import (
	"errors"
	"fmt"
	"net"
	"os"
	"runtime"
	"strconv"
	"strings"
	"syscall"

	"meta_service/common/constants"
)

const (
	normalExitCode             = 1
	AddressAlreadyUsedExitCode = 98
	WSAEADDRINUSE              = 10048
	addressLen                 = 2
	ipIndex                    = 0
	portIndex                  = 1
)

// ProcessBindErrorAndExit will deal with err type
func ProcessBindErrorAndExit(err error) {
	fmt.Printf("failed to listen address, err: %s", err.Error())
	if isErrorAddressAlreadyInUse(err) {
		os.Exit(AddressAlreadyUsedExitCode)
	}
	os.Exit(normalExitCode)
}

func isErrorAddressAlreadyInUse(err error) bool {
	var eOsSyscall *os.SyscallError
	if !errors.As(err, &eOsSyscall) {
		return false
	}
	var errErrno syscall.Errno
	if !errors.As(eOsSyscall, &errErrno) {
		return false
	}
	if errErrno == syscall.EADDRINUSE {
		return true
	}
	if runtime.GOOS == "windows" && errErrno == WSAEADDRINUSE {
		return true
	}
	return false
}

// CheckAddress check whether the address is valid
func CheckAddress(addr string) bool {
	addrArg := strings.Split(addr, ":")
	if len(addrArg) != addressLen {
		return false
	}
	ip := net.ParseIP(addrArg[ipIndex])
	if ip == nil {
		return false
	}
	port, err := strconv.Atoi(addrArg[portIndex])
	if err != nil {
		return false
	}
	if port < 0 || port > constants.MaxPort {
		return false
	}
	return true
}
