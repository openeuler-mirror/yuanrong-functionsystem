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

package publish

import (
	"reflect"
	"testing"

	"meta_service/common/metadata"
	"meta_service/common/types"
)

// TestRootfsFieldAlignment asserts every field of the types.Rootfs* structs
// has a same-named, same-kind field on the metadata.Rootfs* structs, so a
// field added on the source side but not mirrored fails CI immediately instead
// of being silently dropped by buildRootFsSpecMeta's field-by-field copy.
func TestRootfsFieldAlignment(t *testing.T) {
	cases := []struct {
		name string
		src  interface{}
		dst  interface{}
	}{
		{"RootfsSpecMeta", types.RootfsSpecMeta{}, metadata.RootfsSpecMeta{}},
		{"RootfsStorageInfo", types.RootfsStorageInfo{}, metadata.RootfsStorageInfo{}},
		{"RootfsMount", types.RootfsMount{}, metadata.RootfsMount{}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			srcFields := reflect.VisibleFields(reflect.TypeOf(c.src))
			dstType := reflect.TypeOf(c.dst)
			for _, sf := range srcFields {
				dstField, ok := dstType.FieldByName(sf.Name)
				if !ok {
					t.Errorf("%s: types field %q is missing on metadata struct "+
						"-> buildRootFsSpecMeta would silently drop it", c.name, sf.Name)
					continue
				}
				if dstField.Type.Kind() != sf.Type.Kind() {
					t.Errorf("%s: field %q kind mismatch: types=%s metadata=%s",
						c.name, sf.Name, sf.Type.Kind(), dstField.Type.Kind())
				}
			}
		})
	}
}
