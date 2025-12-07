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
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"github.com/agiledragon/gomonkey"
	"github.com/huaweicloud/huaweicloud-sdk-go-obs/obs"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"meta_service/common/crypto"
	commonObs "meta_service/common/obs"
	"meta_service/function_repo/config"
	"meta_service/function_repo/server"
	"meta_service/function_repo/test/fakecontext"
)

func TestObsBucketChooser(t *testing.T) {
	chooser := newObsBucketChooser([]config.BucketConfig{
		{
			BusinessID: "a",
			BucketID:   "xx",
		},
		{
			BusinessID: "b",
			BucketID:   "yy",
		},
		{
			BusinessID: "b",
			BucketID:   "zz",
		},
	})

	bucketID, err := chooser.Choose("a")
	require.NoError(t, err)
	assert.Equal(t, "xx", bucketID)

	_, err = chooser.Choose("not exist")
	assert.Error(t, err)

	cfg, err := chooser.Find("b", "yy")
	require.NoError(t, err)
	assert.Equal(t, "b", cfg.BusinessID)
	assert.Equal(t, "yy", cfg.BucketID)

	_, err = chooser.Find("b", "not exist")
	assert.Error(t, err)

	_, err = chooser.Find("not exist", "not exist")
	assert.Error(t, err)
}

func TestObs(t *testing.T) {
	fakeClient := &obs.ObsClient{}
	var err error

	opt := commonObs.Option{
		Endpoint:      "http://127.0.0.1:9000",
		Secure:        false,
		AccessKey:     "minioadmin",
		SecretKey:     "minioadmin",
		MaxRetryCount: 3,
		Timeout:       60,
	}
	patches := [...]*gomonkey.Patches{
		gomonkey.ApplyFunc(crypto.Decrypt, func(cipherText []byte, secret []byte) (string, error) {
			return "", nil
		}),
		gomonkey.ApplyFunc(commonObs.NewObsClient, func(o commonObs.Option) (*obs.ObsClient, error) {
			return fakeClient, nil
		}),
	}
	defer func() {
		for index := range patches {
			patches[index].Reset()
		}
	}()
	fakeClient, _ = commonObs.NewObsClient(opt)
	config.RepoCfg = &config.Configs{}
	config.RepoCfg.FileServer.StorageType = "s3"
	config.RepoCfg.FunctionCfg.PackageCfg = config.PackageConfig{
		UploadTmpPath:      os.TempDir(),
		ZipFileSizeMaxMB:   100,
		UnzipFileSizeMaxMB: 100,
		FileCountsMax:      100,
		DirDepthMax:        100,
		IOReadTimeout:      1000000,
	}
	config.RepoCfg.BucketCfg = []config.BucketConfig{
		{
			BusinessID: "aa",
			BucketID:   "xxxx",
		},
	}

	c := fakecontext.NewContext()
	c.InitTenantInfo(server.TenantInfo{
		BusinessID: "aa",
		TenantID:   "",
		ProductID:  "",
	})

	// start
	err = Init()
	require.NoError(t, err)

	b, err := newZipFile([]zipFile{
		{filepath.Join("example", "readme.txt"), "This archive contains some text files."},
		{"gopher.txt", "Gopher names:\nGeorge\nGeoffrey\nGonzo"},
		{"todo.txt", "Get animal handling licence.\nWrite more examples."},
	})
	require.NoError(t, err, "create zip file")

	zipbuf := bytes.NewBuffer(b)
	size := int64(zipbuf.Len())
	_, err = NewPackage(c, "obs-test", zipbuf, size)
	require.NoError(t, err)
}

func TestBucketID(t *testing.T) {
	s3Uploader := s3Uploader{
		bucketName: "test",
		objectName: "testObject",
	}
	assert.Equal(t, s3Uploader.BucketID(), s3Uploader.bucketName)
	assert.Equal(t, s3Uploader.ObjectID(), s3Uploader.objectName)
}
