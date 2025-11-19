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

#ifndef LITEBUS_SENSITIVE_VALUE_H
#define LITEBUS_SENSITIVE_VALUE_H

#include <memory>

namespace litebus {
class SensitiveValue {
public:
    SensitiveValue() = default;
    explicit SensitiveValue(const char *str);
    explicit SensitiveValue(const std::string &str);
    SensitiveValue(const char *str, size_t size);
    SensitiveValue(std::unique_ptr<char[]> data, size_t size);

    SensitiveValue(SensitiveValue &&other) noexcept;
    SensitiveValue(const SensitiveValue &other);
    ~SensitiveValue();

    SensitiveValue &operator=(const SensitiveValue &other);
    SensitiveValue &operator=(SensitiveValue &&other) noexcept;
    SensitiveValue &operator=(const char *str);
    SensitiveValue &operator=(const std::string &str);
    bool operator==(const SensitiveValue &other) const;

    bool Empty() const;
    const char *GetData() const;
    std::string GetMaskData() const;
    size_t GetSize() const;
    bool MoveTo(std::unique_ptr<char[]> &outData, size_t &outSize);
    void Clear() noexcept;

private:
    void SetData(const char *str, size_t size);

    std::unique_ptr<char[]> data_ = nullptr;
    size_t size_ = 0;
};
}  // namespace litebus::ssl
#endif