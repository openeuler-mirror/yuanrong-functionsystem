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

#ifndef FUNCTIONSYSTEM_RPC_CLIENT_GRPC_CALL_OBJ_H
#define FUNCTIONSYSTEM_RPC_CLIENT_GRPC_CALL_OBJ_H

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "grpc_cq_tag.h"

namespace functionsystem {

using StatusCallback = std::function<void(const Status &)>;

template <typename T, typename Request, typename Response,
          typename StubFunc = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<Response>> (*)(
              ::grpc::ClientContext *, const Request &, ::grpc::CompletionQueue *)>
class GrpcCallObj : public GrpcCQTag {
public:
    GrpcCallObj(T *stub, ::grpc::CompletionQueue *cq, const ::grpc::string &method, const Request *req, Response *rsp,
                StubFunc stubFunc, StatusCallback done, uint64_t timeoutInMS = 0, bool failFast = true)
        : cq_(cq),
          stub_(stub),
          stubFunc_(stubFunc),
          method_(method),
          rsp_(rsp),
          req_(req),
          done_(std::move(done)),
          timeout_(timeoutInMS),
          failFast_(failFast)
    {
        YRLOG_DEBUG("create grpc call object. call method = {}", method_);
        StartCall();
    }

    ~GrpcCallObj() override = default;

    void StartCall()
    {
        YRLOG_DEBUG("grpc call object start call : {}.", method_);
        context_->set_wait_for_ready(!failFast_);
        if (timeout_ > 0) {
            context_->set_deadline(gpr_time_from_millis(static_cast<int64_t>(timeout_), GPR_TIMESPAN));
        }

        auto reader = (stub_->*stubFunc_)(context_.get(), *req_, cq_);
        reader->Finish(rsp_, &status_, this);
    }

    void OnCompleted(bool ok) override
    {
        YRLOG_DEBUG("grpc call object complete call : {}.", method_);
        if (status_.ok() && !ok) {
            YRLOG_ERROR("this case should never happen : grpc call status is ok, but CompletionQueueStatus is not.");
        }

        Status s = FromGrpcStatus(status_);
        done_(s);
        delete this;
    }

    Status FromGrpcStatus(const ::grpc::Status &status) const
    {
        if (status.ok()) {
            return Status::OK();
        } else {
            auto errorCode = Status::GrpcCode2StatusCode(static_cast<int>(status.error_code()));
            YRLOG_WARN("FromGrpcStatus meets error: {} {}", fmt::underlying(errorCode),
                       ". grpc completion queue error info: " + status.error_message());
            return Status(errorCode, ". grpc completion queue error info: " + status.error_message());
        }
    }

private:
    std::unique_ptr<::grpc::ClientContext> context_ = std::make_unique<::grpc::ClientContext>();
    ::grpc::Status status_;
    ::grpc::CompletionQueue *cq_;
    T *stub_;
    StubFunc stubFunc_;
    ::grpc::string method_;
    Response *rsp_;
    const Request *req_;

    StatusCallback done_;
    uint64_t timeout_; /* ms */
    bool failFast_;
};

}  // namespace functionsystem

#endif