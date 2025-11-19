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

#ifndef FUNCTIONSYSTEM_META_STORE_KEY_VALUE_CLIENT_H
#define FUNCTIONSYSTEM_META_STORE_KEY_VALUE_CLIENT_H

#include <async/future.hpp>
#include <string>

#include "meta_store_struct.h"
#include "txn_transaction.h"

namespace functionsystem::meta_store {

class KeyValueClient {
public:
    virtual ~KeyValueClient() = default;

    /**
     * put a key-value pair into MetaStore.
     *
     * @param key the key or prefix to put.
     * @param value the value to put.
     * @param option the option of request.
     * @return the response of request.
     */
    virtual litebus::Future<std::shared_ptr<PutResponse>> Put(const std::string &key, const std::string &value,
                                                              const PutOption &option) = 0;

    /**
     * delete a key-value pair from MetaStore.
     *
     * @param key the key or prefix to delete.
     * @param option the option of request.
     * @return the response of request.
     */
    virtual litebus::Future<std::shared_ptr<DeleteResponse>> Delete(const std::string &key,
                                                                    const DeleteOption &option) = 0;

    /**
     * get some key-value pairs from MetaStore.
     *
     * @param key the key or prefix to get.
     * @param option the option of request.
     * @return the response of request.
     */
    virtual litebus::Future<std::shared_ptr<GetResponse>> Get(const std::string &key, const GetOption &option) = 0;

    /**
     * create a transaction.
     *
     * @return a transaction.
     */
    virtual std::shared_ptr<TxnTransaction> BeginTransaction() = 0;

    /**
     * commit a transaction request
     *
     * @param request txn request in etcd pb
     * @param asyncBackup is need async back up
     * @return transaction response
     */
    virtual litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> Commit(
        const ::etcdserverpb::TxnRequest &request, bool asyncBackup) = 0;

protected:
    KeyValueClient() = default;
};
}  // namespace functionsystem::meta_store
#endif  // FUNCTIONSYSTEM_META_STORE_KEY_VALUE_CLIENT_H
