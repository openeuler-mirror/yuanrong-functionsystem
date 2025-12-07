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
	"crypto/sha512"
	"encoding/hex"
	"errors"
	"hash"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"golang.org/x/text/unicode/norm"

	"meta_service/common/logger/log"
	"meta_service/common/reader"
	"meta_service/common/snerror"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
)

var (
	defaultHasher = sha512.New

	zipFileDirRegex *regexp.Regexp = regexp.MustCompile("")

	mbShift = 20
)

const uploadPathMode = 0700

type zipPackageBuilder struct {
	TempFileDir         string // must be absolute
	TempFilePattern     string
	MaxZipFileSize      int64
	MaxZipFileUnzipSize int64
	MaxZipFileCount     int
	MaxZipFileDepth     int
	MaxZipFileTimeout   int
	Hasher              func() hash.Hash
}

func newZipPackageBuilder(cfg config.PackageConfig) (PackageBuilder, error) {
	if cfg.UploadTmpPath == "" {
		cfg.UploadTmpPath = os.TempDir()
	} else {
		abs, err := filepath.Abs(cfg.UploadTmpPath)
		if err != nil {
			log.GetLogger().Errorf("invalid upload tmp path: %s", err.Error())
			return nil, err
		}
		if err := mkdirUploadPathIfNotExists(abs); err != nil {
			log.GetLogger().Errorf("invalid upload tmp path: %s", err.Error())
			return nil, err
		}
		cfg.UploadTmpPath = abs
	}

	return &zipPackageBuilder{
		TempFileDir:         cfg.UploadTmpPath,
		TempFilePattern:     "function-repository*",
		MaxZipFileSize:      int64(cfg.ZipFileSizeMaxMB << mbShift),
		MaxZipFileUnzipSize: int64(cfg.UnzipFileSizeMaxMB << mbShift),
		MaxZipFileCount:     int(cfg.FileCountsMax),
		MaxZipFileDepth:     int(cfg.DirDepthMax),
		MaxZipFileTimeout:   int(cfg.IOReadTimeout),
		Hasher:              defaultHasher,
	}, nil
}

func mkdirUploadPathIfNotExists(path string) error {
	if _, err := reader.ReadFileInfoWithTimeout(path); err != nil {
		if os.IsExist(err) {
			return err
		}
		if err := os.Mkdir(path, uploadPathMode); err != nil {
			return err
		}
	}
	return nil
}

// PackageWriter -
type PackageWriter struct {
	StopCh     <-chan time.Time
	writerHash hash.Hash
}

// Write processes the slices of the reading file by distinguish whether the timer stops or runs normally
func (pw *PackageWriter) Write(p []byte) (int, error) {
	select {
	case <-pw.StopCh:
		return 0, snerror.New(errmsg.ReadingPackageTimeout, "timeout while writing a package")
	default:
		// Executes the normal reading logic
		return pw.writerHash.Write(p)
	}
}

// NewPackage implements PackageBuilder
func (b *zipPackageBuilder) NewPackage(c server.Context, name string, reader io.Reader, n int64) (Package, error) {
	if n > b.MaxZipFileSize {
		log.GetLogger().Errorf("zip file size [%d] is larger than %d", n, b.MaxZipFileSize)
		return nil, snerror.NewWithFmtMsg(
			errmsg.ZipFileSizeError, "zip file size [%d] is larger than %d", n, b.MaxZipFileSize)
	}
	file, err := ioutil.TempFile(b.TempFileDir, b.TempFilePattern)
	if err != nil {
		return nil, snerror.New(errmsg.SaveFileError, "failed to create temp file")
	}
	defer func() {
		err = file.Close()
		if err != nil {
			err2 := os.Remove(file.Name())
			if err2 != nil {
				log.GetLogger().Errorf("clean temp file error: %s", err2.Error())
			}
		}
	}()

	h := b.Hasher()
	timer := time.NewTimer(time.Duration(b.MaxZipFileTimeout) * time.Millisecond)
	writer := &PackageWriter{
		StopCh:     timer.C,
		writerHash: h,
	}
	r := io.TeeReader(reader, writer)
	if written, err := io.CopyN(file, r, n); err != nil {
		var snErr snerror.SNError
		if errors.As(err, &snErr) {
			log.GetLogger().Errorf("failed to read a package file as meeting a timeout,"+
				" where the function name is %s", name)
			return nil, err
		}
		log.GetLogger().Errorf("failed to write temp file: %s, written=%d, n=%d", err.Error(), written, n)
		return nil, snerror.New(errmsg.SaveFileError, "failed to write temp file")
	}

	if err = b.validateFile(file.Name()); err != nil {
		log.GetLogger().Errorf("zip file %s is not valid: %s", name, err.Error())
		return nil, err
	}

	return &zipPackage{
		name:      name,
		fileName:  file.Name(),
		signature: hex.EncodeToString(h.Sum(nil)),
		size:      n,
	}, nil
}

func (b *zipPackageBuilder) validateFile(filename string) error {
	r, err := zip.OpenReader(filename)
	if err != nil {
		return snerror.NewWithError(errmsg.ZipFileError, err)
	}
	defer r.Close()

	if len(r.File) > b.MaxZipFileCount {
		return snerror.NewWithFmtMsg(
			errmsg.ZipFileCountError, "zip file count [%v] is larger than %v", len(r.File), b.MaxZipFileCount)
	}

	var total int64
	for _, file := range r.File {
		total += int64(file.UncompressedSize64) // assume int64 is big enough that this never wraps
		if total > b.MaxZipFileUnzipSize {
			return snerror.NewWithFmtMsg(
				errmsg.ZipFileUnzipSizeError, "zip file unzip size is larger than %v", b.MaxZipFileUnzipSize)
		}

		if !b.validateFilePath(filename, file.Name) {
			return snerror.NewWithFmtMsg(errmsg.ZipFilePathError, "zip file path [%s] is invalid", file.Name)
		}
	}

	return nil
}

func (b *zipPackageBuilder) validateFilePath(zipfile, path string) bool {
	if path == "" ||
		strings.Contains(path, "~/") ||
		zipFileDirRegex == nil ||
		!zipFileDirRegex.MatchString(norm.NFC.String(path)) ||
		len(strings.Split(path, string(filepath.Separator))) > b.MaxZipFileDepth {

		return false
	}

	abs, err := filepath.Abs(filepath.Join(zipfile, path))
	if err != nil {
		return false
	}

	return strings.HasPrefix(abs, b.TempFileDir)
}

type zipPackage struct {
	name      string
	fileName  string
	signature string
	size      int64
}

// Close implements Package
func (p *zipPackage) Close() error {
	if p.fileName == "" {
		return nil
	}

	err := os.Remove(p.fileName)
	if err != nil {
		log.GetLogger().Errorf("failed to delete temporary file %s", p.fileName)
		return snerror.NewWithFmtMsg(errmsg.ZipFileError, "failed to delete temporary file %s", p.fileName)
	}
	return nil
}

// Name implements Package
func (p *zipPackage) Name() string {
	return p.name
}

// FileName implements Package
func (p *zipPackage) FileName() string {
	return p.fileName
}

// Move implements Package
func (p *zipPackage) Move() string {
	fileName := p.fileName
	p.fileName = ""
	return fileName
}

// Signature implements Package
func (p *zipPackage) Signature() string {
	return p.signature
}

// Size implements Package
func (p *zipPackage) Size() int64 {
	return p.size
}
