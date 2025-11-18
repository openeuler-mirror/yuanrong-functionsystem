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

#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_CALL_CACHE_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_CALL_CACHE_H

#include <list>
#include <unordered_map>

#include "async/future.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"

namespace functionsystem::busproxy {
struct CallRequestContext {
    std::string from;
    std::string requestID;
    std::string traceID;
    std::string callerTenantID;
    SharedStreamMsg callRequest;
    litebus::Promise<SharedStreamMsg> callResponse;
};

class CallCache {
public:
    void Push(const std::shared_ptr<CallRequestContext> &context);

    void PushOnResp(const std::shared_ptr<CallRequestContext> &context);

    void MoveToOnResp(const std::string &requestID);

    void MoveToInProgress(const std::string &requestID);

    std::shared_ptr<CallRequestContext> FindCallRequestContext(const std::string &requestID);

    void DeleteReqInProgress(const std::string &requestID);

    void DeleteReqNew(const std::string &requestID);

    void DeleteReqOnResp(const std::string &requestID);

    std::unordered_set<std::string> GetNewReqs();

    std::unordered_set<std::string> GetOnResp();

    std::unordered_set<std::string> GetInProgressReqs();

    std::list<litebus::Future<SharedStreamMsg>> GetOnRespFuture();

    void MoveAllToNew();

private:
    std::unordered_map<std::string, std::shared_ptr<CallRequestContext>> requestMap_;
    std::unordered_set<std::string> reqNew_;
    std::unordered_set<std::string> reqInProgress_;
    std::unordered_set<std::string> reqOnResp_;
    std::unordered_map<std::string, litebus::Promise<CallResultAck>> callResultAckPromises_;
};

}  // namespace functionsystem::busproxy

#endif  // FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_CALL_CACHE_H
