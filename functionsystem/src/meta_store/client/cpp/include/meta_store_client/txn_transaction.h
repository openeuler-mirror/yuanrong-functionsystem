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

#ifndef FUNCTIONSYSTEM_META_STORE_TXN_TRANSACTION_H
#define FUNCTIONSYSTEM_META_STORE_TXN_TRANSACTION_H

#include <async/future.hpp>
#include <string>
#include <variant>

#include "common/logs/logging.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "etcd/api/etcdserverpb/rpc.pb.h"
#include "meta_store_struct.h"
#include "utils/string_util.h"

namespace functionsystem::meta_store {
enum class TargetType : int { VERSION = 0, CREATE = 1, MODIFY = 2, VALUE = 3, LEASE = 4 };

// logical operator
enum class CompareOperator : int { EQUAL = 0, GREATER = 1, LESS = 2, NOT_EQUAL = 3 };

class TxnCompare {
public:
    static TxnCompare OfVersion(const std::string &key, const CompareOperator &op, int64_t value)
    {
        TxnCompare ret(key, TargetType::VERSION);
        ret.operator_ = op;
        ret.targetValue_ = value;
        return ret;
    }

    static TxnCompare OfCreateVersion(const std::string &key, const CompareOperator &op, int64_t value)
    {
        TxnCompare ret(key, TargetType::CREATE);
        ret.operator_ = op;
        ret.targetValue_ = value;
        return ret;
    }

    static TxnCompare OfModifyVersion(const std::string &key, const CompareOperator &op, int64_t value)
    {
        TxnCompare ret(key, TargetType::MODIFY);
        ret.operator_ = op;
        ret.targetValue_ = value;
        return ret;
    }

    static TxnCompare OfValue(const std::string &key, const CompareOperator &op, const std::string &value)
    {
        TxnCompare ret(key, TargetType::VALUE);
        ret.operator_ = op;
        ret.targetValue_ = value;
        return ret;
    }

    static TxnCompare OfLease(const std::string &key, const CompareOperator &op, int64_t value)
    {
        TxnCompare ret(key, TargetType::LEASE);
        ret.operator_ = op;
        ret.targetValue_ = value;
        return ret;
    }

    ~TxnCompare() = default;

    void Build(::etcdserverpb::Compare &compare, const std::string &prefix) const
    {
        std::string realKey = prefix + key_;
        compare.set_key(realKey);

        switch (targetType_) {
            case TargetType::VERSION:
                compare.set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_VERSION);
                compare.set_version(std::get<int64_t>(targetValue_));
                break;
            case TargetType::CREATE:
                compare.set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_CREATE);
                compare.set_create_revision(std::get<int64_t>(targetValue_));
                break;
            case TargetType::MODIFY:
                compare.set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_MOD);
                compare.set_mod_revision(std::get<int64_t>(targetValue_));
                break;
            case TargetType::VALUE:
                compare.set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_VALUE);
                compare.set_value(std::get<std::string>(targetValue_));
                break;
            case TargetType::LEASE:
                compare.set_target(::etcdserverpb::Compare_CompareTarget::Compare_CompareTarget_LEASE);
                compare.set_lease(std::get<int64_t>(targetValue_));
                break;
            default:
                break;
        }

        switch (operator_) {
            case CompareOperator::EQUAL:
                compare.set_result(etcdserverpb::Compare_CompareResult_EQUAL);
                break;
            case CompareOperator::GREATER:
                compare.set_result(etcdserverpb::Compare_CompareResult_GREATER);
                break;
            case CompareOperator::LESS:
                compare.set_result(etcdserverpb::Compare_CompareResult_LESS);
                break;
            case CompareOperator::NOT_EQUAL:
                compare.set_result(etcdserverpb::Compare_CompareResult_NOT_EQUAL);
                break;
            default:
                break;
        }
    }

private:
    std::string key_;

    TargetType targetType_;

    // logical operator
    CompareOperator operator_;

    std::variant<int64_t, std::string> targetValue_;

    TxnCompare(const std::string &key, const TargetType &targetType)
        : key_(key), targetType_(targetType), operator_(CompareOperator::EQUAL)
    {
    }
};

class TxnOperation {
public:
    static TxnOperation Create(const std::string &key, const std::string &value, const PutOption &option)
    {
        TxnOperation operation(TxnOperationType::OPERATION_PUT, key);
        operation.option_ = option;  // PutOption
        operation.value_ = value;
        return operation;
    }

    static TxnOperation Create(const std::string &key, const DeleteOption &option)
    {
        TxnOperation operation(TxnOperationType::OPERATION_DELETE, key);
        operation.option_ = option;  // DeleteOption
        return operation;
    }

    static TxnOperation Create(const std::string &key, const GetOption &option)
    {
        TxnOperation operation(TxnOperationType::OPERATION_GET, key);
        operation.option_ = option;  // GetOption
        return operation;
    }

    TxnOperation() = delete;

    void Build(::etcdserverpb::RequestOp &requestOp, const std::string &prefix) const
    {
        switch (type_) {
            case TxnOperationType::OPERATION_PUT:
                Build(*requestOp.mutable_request_put(), prefix);
                break;
            case TxnOperationType::OPERATION_DELETE:
                Build(*requestOp.mutable_request_delete_range(), prefix);
                break;
            case TxnOperationType::OPERATION_GET:
                Build(*requestOp.mutable_request_range(), prefix);
                break;
            default:
                break;
        }
    }

    bool GetAsyncBackupOption() const
    {
        switch (type_) {
            case TxnOperationType::OPERATION_PUT: {
                PutOption putOpt = std::get<PutOption>(option_);
                return putOpt.asyncBackup;
            }
            case TxnOperationType::OPERATION_DELETE: {
                DeleteOption delOpt = std::get<DeleteOption>(option_);
                return delOpt.asyncBackup;
            }
            case TxnOperationType::OPERATION_GET:
                return true;
            default:
                break;
        }
        return true;
    }

private:
    TxnOperationType type_;  // must

    std::string key_;  // must

    std::string value_;  // only for put

    std::variant<PutOption, DeleteOption, GetOption> option_;

    TxnOperation(const TxnOperationType &type, std::string key) : type_(type), key_(std::move(key))
    {
    }

    void Build(::etcdserverpb::PutRequest &request, const std::string &prefix) const
    {
        PutOption option = std::get<PutOption>(option_);
        std::string realKey = prefix + key_;
        request.set_key(realKey);
        request.set_value(value_);
        request.set_lease(option.leaseId);
        request.set_prev_kv(option.prevKv);
    }

    void Build(::etcdserverpb::DeleteRangeRequest &request, const std::string &prefix) const
    {
        DeleteOption option = std::get<DeleteOption>(option_);
        std::string realKey = prefix + key_;
        request.set_key(realKey);
        if (option.prefix) {  // prefix
            request.set_range_end(StringPlusOne(realKey));
        }
        request.set_prev_kv(option.prevKv);
    }

    void Build(::etcdserverpb::RangeRequest &request, const std::string &prefix) const
    {
        GetOption option = std::get<GetOption>(option_);
        std::string realKey = prefix + key_;
        request.set_key(realKey);
        if (option.prefix) {  // prefix
            request.set_range_end(StringPlusOne(realKey));
        }
        request.set_limit(option.limit);
        request.set_keys_only(option.keysOnly);
        request.set_count_only(option.countOnly);
        request.set_sort_order(static_cast<etcdserverpb::RangeRequest_SortOrder>(option.sortOrder));
        request.set_sort_target(static_cast<etcdserverpb::RangeRequest_SortTarget>(option.sortTarget));
    }
};

class TxnTransaction {
public:
    explicit TxnTransaction(const litebus::AID &actorAid)
    {
        actorAid_ = actorAid;
    }

    virtual ~TxnTransaction() = default;

    void If(const TxnCompare &compare)
    {
        if (!thenOps.empty()) {
            YRLOG_WARN("can not if after then");
        }

        if (!elseOps.empty()) {
            YRLOG_WARN("can not if after else");
        }

        (void)compares.emplace_back(compare);
    }

    void Then(const TxnOperation &operation)
    {
        if (!elseOps.empty()) {
            YRLOG_WARN("can not then after else");
        }

        (void)thenOps.emplace_back(operation);
    }

    void Else(const TxnOperation &operation)
    {
        (void)elseOps.emplace_back(operation);
    }

    virtual litebus::Future<std::shared_ptr<TxnResponse>> Commit() const;

protected:
    litebus::AID actorAid_;

    std::vector<TxnCompare> compares;

    std::vector<TxnOperation> thenOps;

    std::vector<TxnOperation> elseOps;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTIONSYSTEM_META_STORE_TXN_TRANSACTION_H
