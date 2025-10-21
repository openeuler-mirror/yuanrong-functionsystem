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
	"fmt"
	"os"
)

// CreateSymbolicLinkWithForce creates a symbolic link from target to link.
// If the target path already exists (whether it's a file, directory, or symlink), it will be removed first.
func CreateSymbolicLinkWithForce(target, link string) error {
	linkInfo, err := os.Lstat(link)
	if err == nil {
		if linkInfo.Mode()&os.ModeSymlink != 0 {
			err = os.Remove(link)
			if err != nil {
				return fmt.Errorf("failed to remove existing symlink: %v", err)
			}
		} else if linkInfo.IsDir() {
			err = os.RemoveAll(link)
			if err != nil {
				return fmt.Errorf("failed to remove existing directory: %v", err)
			}
		} else {
			err = os.Remove(link)
			if err != nil {
				return fmt.Errorf("failed to remove existing file: %v", err)
			}
		}
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("failed to check existing symlink: %v", err)
	}

	err = os.Symlink(target, link)
	if err != nil {
		return fmt.Errorf("failed to create symlink: %v", err)
	}

	return nil
}
