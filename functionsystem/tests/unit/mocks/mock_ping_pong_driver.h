#ifndef UT_MOCKS_MOCK_PING_PONG_DRIVER_H
#define UT_MOCKS_MOCK_PING_PONG_DRIVER_H

#include <gmock/gmock.h>

#include "heartbeat/ping_pong_driver.h"

namespace functionsystem::test {
class MockPingPongDriver : public PingPongDriver {
public:
    MockPingPongDriver() : PingPongDriver("MockPingPongDriver", 12000, [](const litebus::AID &, HeartbeatConnection) {})
    {
    }
    ~MockPingPongDriver() = default;
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_PING_PONG_DRIVER_H
