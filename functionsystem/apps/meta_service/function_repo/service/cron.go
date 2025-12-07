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

package service

import (
	"time"

	"k8s.io/apimachinery/pkg/util/wait"

	"meta_service/common/logger/log"
	"meta_service/function_repo/pkgstore"
	"meta_service/function_repo/storage"
)

const (
	cronDuration = 24 * time.Hour
)

// CRON runs CRON jobs. A typical CRON job involves cleaning up invalid resources / unwanted status left by a previous
// function-repository session.
func CRON(stopCh <-chan struct{}) {
	wait.Until(func() {
		deleteUncontrolledPackages()
	}, cronDuration, stopCh)
	log.GetLogger().Warnf("received termination signal to shutdown")
}

func deleteUncontrolledPackages() {
	tuples, err := storage.GetUncontrolledList()
	if err != nil {
		log.GetLogger().Warnf("failed to get uncontrolled list: %s", err.Error())
		return
	}
	for _, tuple := range tuples {
		if err := pkgstore.Delete(tuple.Key.BucketID, tuple.Key.ObjectID); err != nil {
			log.GetLogger().Warnf("failed to delete package from uncontrolled list, bucketID: %s, objectID: %s",
				tuple.Key.BucketID, tuple.Key.ObjectID)
			continue
		}
		if err := storage.RemoveUncontrolledByKey(tuple.Key); err != nil {
			log.GetLogger().Warnf("failed to remove uncontrolled by key: %s", err.Error())
			continue
		}
		log.GetLogger().Debugf("delete uncontrolled package %+v successfully", tuple)
	}
}
