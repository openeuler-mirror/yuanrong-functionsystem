#pragma once

#include <chrono>
#include <thread>

namespace testing::internal {

inline void SleepMilliseconds(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

} // namespace testing::internal
