/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "function_master/instance_manager/quota_manager/quota_manager.h"

namespace function_master {
QuotaManager::QuotaManager(litebus::ActorReference actor) : actor_(std::move(actor)) {}
}  // namespace function_master
