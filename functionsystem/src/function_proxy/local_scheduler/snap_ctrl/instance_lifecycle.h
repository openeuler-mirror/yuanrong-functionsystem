/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef LOCAL_SCHEDULER_SNAP_CTRL_INSTANCE_LIFECYCLE_H
#define LOCAL_SCHEDULER_SNAP_CTRL_INSTANCE_LIFECYCLE_H

#include <cstdint>

namespace functionsystem::local_scheduler {

struct DeletePreparation {
    uint64_t generation{ 0 };
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_SNAP_CTRL_INSTANCE_LIFECYCLE_H
