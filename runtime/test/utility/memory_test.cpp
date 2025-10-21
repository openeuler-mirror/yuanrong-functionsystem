#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/utility/memory.h"

namespace YR {
namespace test {
using namespace YR::utility;
class MemoryUtilityTest : public ::testing::Test {
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(MemoryUtilityTest, CopyInParallel)
{
    const size_t dataSize = 1024;
    const size_t blockSize = 64;
    const int64_t totalBytes = dataSize;

    uint8_t *src = new uint8_t[dataSize];
    uint8_t *dst = new uint8_t[dataSize];

    for (size_t i = 0; i < dataSize; ++i) {
        src[i] = static_cast<uint8_t>(i);
    }

    CopyInParallel(dst, src, totalBytes, blockSize);

    for (size_t i = 0; i < dataSize; ++i) {
        EXPECT_EQ(src[i], dst[i]);
    }
    delete[] src;
    delete[] dst;
}
}  // namespace test
}  // namespace YR