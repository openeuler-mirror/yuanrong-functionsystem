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

#ifndef SRC_DOMAIN_SCHEDULER_STARTUP_DOMAIN_SCHEDUELR_DRIVER_H
#define SRC_DOMAIN_SCHEDULER_STARTUP_DOMAIN_SCHEDUELR_DRIVER_H

#include "domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.h"
#include "domain_scheduler/instance_control/instance_ctrl_actor.h"
#include "common/schedule_decision/schedule_queue_actor.h"
#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr_actor.h"
#include "domain_scheduler/domain_group_control/domain_group_ctrl_actor.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/utils/module_driver.h"
#include "domain_scheduler/include/structure.h"

namespace functionsystem::domain_scheduler {
class DomainSchedulerDriver : public ModuleDriver {
public:
    explicit DomainSchedulerDriver(const DomainSchedulerParam &param);
    ~DomainSchedulerDriver() override = default;

    Status Start() override;
    Status Stop() override;
    void Await() override;

    // only for test
    void SetSchedulePlugins(const std::string &schedulePlugins)
    {
        param_.schedulePlugins = schedulePlugins;
    }

protected:
    Status RegisterPolicy(std::shared_ptr<schedule_decision::Scheduler> scheduler);
    std::shared_ptr<schedule_decision::ScheduleQueueActor> CreateScheduler(
        const std::string &tag,
        const std::shared_ptr<schedule_decision::ScheduleRecorder> &scheduleRecorder,
        const std::shared_ptr<resource_view::ResourceView> resourceView);

private:
    DomainSchedulerParam param_;
    std::shared_ptr<DomainSchedSrvActor> domainSrvActor_;
    std::shared_ptr<UnderlayerSchedMgrActor> underlayerMgrActor_;
    std::shared_ptr<InstanceCtrlActor> instanceCtrlActor_;
    std::shared_ptr<schedule_decision::ScheduleQueueActor> primaryScheduleQueueActor_;
    std::shared_ptr<schedule_decision::ScheduleQueueActor> virtualScheduleQueueActor_;

    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_;
    std::shared_ptr<DomainGroupCtrlActor> domainGroupCtrlActor_;
    bool alreadyStarted{ false };
};
}

#endif  // SRC_DOMAIN_SCHEDULER_STARTUP_DOMAIN_SCHEDUELR_DRIVER_H
