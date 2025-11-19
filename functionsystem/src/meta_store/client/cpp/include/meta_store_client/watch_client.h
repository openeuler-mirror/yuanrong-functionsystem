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

#ifndef FUNCTIONSYSTEM_META_STORE_WATCH_CLIENT_H
#define FUNCTIONSYSTEM_META_STORE_WATCH_CLIENT_H

#include <async/future.hpp>
#include <functional>
#include <string>

#include "meta_store_client/key_value/watcher.h"
#include "common/metadata/metadata.h"
#include "meta_store_struct.h"

namespace functionsystem::meta_store {
class WatchClient {
public:
    virtual ~WatchClient() = default;

    /**
     * watch on a key with option.
     *
     * @param key key to be watched on.
     * @param option see WatchOption.
     * @param observer the event consumer.
     * @return the watcher to stop watching
     */
    virtual litebus::Future<std::shared_ptr<Watcher>> Watch(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
        const SyncerFunction &syncer) = 0;

    virtual litebus::Future<std::shared_ptr<Watcher>> GetAndWatch(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
        const SyncerFunction &syncer) = 0;

    virtual litebus::Future<std::shared_ptr<Watcher>> GetAndWatchWithHandler(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer, const SyncerFunction &syncer,
        const ResponseHandler &handler) = 0;

protected:
    WatchClient() = default;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTIONSYSTEM_META_STORE_WATCH_CLIENT_H
