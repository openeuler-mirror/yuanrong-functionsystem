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

#ifndef FUNCTIONSYSTEM_SRC_COMMON_META_STORE_CLIENT_UTILS_ETCD_UTIL_H
#define FUNCTIONSYSTEM_SRC_COMMON_META_STORE_CLIENT_UTILS_ETCD_UTIL_H

#include "meta_store_client/meta_store_struct.h"
#include "etcd/api/etcdserverpb/rpc.pb.h"

namespace functionsystem::meta_store {

/**
 * transform etcd header to metastore header
 *
 * @param output metastore header
 * @param input etcd header
 */
inline void Transform(ResponseHeader &output, const ::etcdserverpb::ResponseHeader &input)
{
    output.clusterId = input.cluster_id();
    output.memberId = input.member_id();
    output.revision = input.revision();
    output.raftTerm = input.raft_term();
}

/**
 * transform metastore header to etcd header
 *
 * @param output etcd header
 * @param input metastore header
 */
inline void Transform(::etcdserverpb::ResponseHeader *output, const ResponseHeader &input)
{
    output->set_revision(input.revision);
    output->set_raft_term(input.raftTerm);
    output->set_member_id(input.memberId);
    output->set_cluster_id(input.clusterId);
}

}  // namespace functionsystem::meta_store

#endif  // FUNCTIONSYSTEM_SRC_COMMON_META_STORE_CLIENT_UTILS_ETCD_UTIL_H
