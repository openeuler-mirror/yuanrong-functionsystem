/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_H

#include "actor/actor.hpp"

namespace function_master {

class QuotaManager {
public:
    explicit QuotaManager(litebus::ActorReference actor);
    ~QuotaManager() = default;

private:
    litebus::ActorReference actor_;
};

}  // namespace function_master
#endif
