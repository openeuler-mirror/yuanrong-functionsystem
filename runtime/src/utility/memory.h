/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#pragma once

#include <cstddef>
#include <cstdint>
namespace YR {
namespace utility {
void CopyInParallel(uint8_t *dst, const uint8_t *src, int64_t totalBytes, size_t blockSize);
}
}  // namespace YR