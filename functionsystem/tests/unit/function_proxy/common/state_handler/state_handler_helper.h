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

#ifndef UNIT_FUNCTION_PROXY_COMMON_STATE_HANDLER_STATE_HANDLER_HELPER_H
#define UNIT_FUNCTION_PROXY_COMMON_STATE_HANDLER_STATE_HANDLER_HELPER_H

#include <gmock/gmock.h>

#include "function_proxy/common/state_handler/state_handler.h"

namespace functionsystem::test {

class StateHandlerHelper : public function_proxy::StateHandler {
public:
    static void ClearStateActorHelper() {
        StateHandler::ClearStateActor();
    }
};

}  // namespace functionsystem::test

#endif  // UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_FUNCTION_AGENT_MANAGER_FUNCTION_AGENT_HELPER_H
