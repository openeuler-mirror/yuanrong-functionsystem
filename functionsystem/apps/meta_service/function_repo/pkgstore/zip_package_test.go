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

package pkgstore

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"errors"
	"math"
	"os"
	"path/filepath"
	"testing"

	"github.com/agiledragon/gomonkey"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"meta_service/function_repo/config"
	"meta_service/function_repo/test/fakecontext"
)

type zipFile struct {
	Name, Body string
}

func newZipFile(files []zipFile) ([]byte, error) {
	buf := new(bytes.Buffer)

	w := zip.NewWriter(buf)
	for _, file := range files {
		f, err := w.Create(file.Name)
		if err != nil {
			return nil, err
		}

		_, err = f.Write([]byte(file.Body))
		if err != nil {
			return nil, err
		}
	}

	err := w.Close()
	if err != nil {
		return nil, err
	}

	return buf.Bytes(), nil
}

func TestZipPackage(t *testing.T) {
	b, err := newZipFile([]zipFile{
		{filepath.Join("example", "readme.txt"), "This archive contains some text files."},
		{"gopher.txt", "Gopher names:\nGeorge\nGeoffrey\nGonzo"},
		{"todo.txt", "Get animal handling licence.\nWrite more examples."},
	})
	require.NoError(t, err, "create zip file")

	zipbuf := bytes.NewBuffer(b)
	c := fakecontext.NewContext()
	builder, _ := newZipPackageBuilder(config.PackageConfig{
		UploadTmpPath:      os.TempDir(),
		ZipFileSizeMaxMB:   100,
		UnzipFileSizeMaxMB: 100,
		FileCountsMax:      100,
		DirDepthMax:        100,
		IOReadTimeout:      1000000,
	})

	size := int64(zipbuf.Len())
	pkg, err := builder.NewPackage(c, "test", zipbuf, size)
	require.NoError(t, err, "new package")
	assert.Equal(t, "test", pkg.Name(), "pkg name")
	assert.Equal(t, size, pkg.Size(), "pkg size")

	defer pkg.Close()
}

func TestZipPackageValidate(t *testing.T) {
	files := []zipFile{
		{filepath.Join("example", "readme.txt"), "This archive contains some text files."},
		{"gopher.txt", "Gopher names:\nGeorge\nGeoffrey\nGonzo"},
		{"todo.txt", "Get animal handling licence.\nWrite more examples."},
	}
	b, err := newZipFile(files)
	require.NoError(t, err, "create zip file")
	fileSize := int64(len(b))
	unzipSize := func() (total int64) {
		for _, file := range files {
			total += int64(len(file.Body))
		}
		return
	}()

	tests := []struct {
		Name                string
		MaxZipFileSize      int64
		MaxZipFileUnzipSize int64
		MaxZipFileCount     int
		MaxZipFileDepth     int
		NoError             bool
	}{
		{
			Name:                "MaxZipFileSize1",
			MaxZipFileSize:      fileSize,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     100,
			NoError:             true,
		},
		{
			Name:                "MaxZipFileSize1",
			MaxZipFileSize:      fileSize - 1,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     100,
			NoError:             false,
		},
		{
			Name:                "MaxZipFileUnzipSize1",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: unzipSize,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     100,
			NoError:             true,
		},
		{
			Name:                "MaxZipFileUnzipSize2",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: unzipSize - 1,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     100,
			NoError:             false,
		},
		{
			Name:                "MaxZipFileCount",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     len(files),
			MaxZipFileDepth:     100,
			NoError:             true,
		},
		{
			Name:                "MaxZipFileCount",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     len(files) - 1,
			MaxZipFileDepth:     100,
			NoError:             false,
		},
		{
			Name:                "MaxZipFileCount",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     2,
			NoError:             true,
		},
		{
			Name:                "MaxZipFileCount",
			MaxZipFileSize:      math.MaxInt64,
			MaxZipFileUnzipSize: math.MaxInt64,
			MaxZipFileCount:     100,
			MaxZipFileDepth:     1,
			NoError:             false,
		},
	}

	for _, test := range tests {
		t.Run(test.Name, func(t *testing.T) {
			zipbuf := bytes.NewBuffer(b)
			c := fakecontext.NewContext()
			builder := &zipPackageBuilder{
				TempFileDir:         os.TempDir(),
				TempFilePattern:     "function-repository-test",
				MaxZipFileSize:      test.MaxZipFileSize,
				MaxZipFileUnzipSize: test.MaxZipFileUnzipSize,
				MaxZipFileCount:     test.MaxZipFileCount,
				MaxZipFileDepth:     test.MaxZipFileDepth,
				Hasher:              sha256.New,
				MaxZipFileTimeout:   10000,
			}
			pkg, err := builder.NewPackage(c, "test", zipbuf, int64(zipbuf.Len()))
			if test.NoError {
				assert.NoError(t, err, "new package")
				defer pkg.Close()
			} else {
				assert.Error(t, err, "new package")
			}
		})
	}
}

func Test_mkdirUploadPathIfNotExists(t *testing.T) {
	patches := gomonkey.NewPatches()
	defer patches.Reset()

	patches.ApplyFunc(os.Stat, func(_ string) (os.FileInfo, error) { return nil, errors.New("fake error! ") })
	assert.NotNil(t, mkdirUploadPathIfNotExists("test/path"))
}
