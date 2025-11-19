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

#include <securec.h>

#include "actor/buslog.hpp"
#include "ssl/sensitive_value.hpp"

namespace litebus {
SensitiveValue::SensitiveValue(const char *str)
{
    SetData(str, str == nullptr ? 0 : std::strlen(str));
}

SensitiveValue::SensitiveValue(const std::string &str)
{
    SetData(str.data(), str.length());
}

SensitiveValue::SensitiveValue(const char *str, size_t size)
{
    SetData(str, size);
}

SensitiveValue::SensitiveValue(std::unique_ptr<char[]> data, size_t size) : data_(std::move(data)), size_(size)
{
}

SensitiveValue::SensitiveValue(SensitiveValue &&other) noexcept : data_(std::move(other.data_)), size_(other.size_)
{
    other.size_ = 0;
}

SensitiveValue::SensitiveValue(const SensitiveValue &other)
{
    if (!other.Empty()) {
        SetData(other.data_.get(), other.size_);
    }
}

SensitiveValue::~SensitiveValue()
{
    Clear();
}

SensitiveValue &SensitiveValue::operator=(const SensitiveValue &other)
{
    if (this == &other) {
        return *this;
    }
    Clear();
    if (!other.Empty()) {
        SetData(other.data_.get(), other.size_);
    }
    return *this;
}

SensitiveValue &SensitiveValue::operator=(SensitiveValue &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    Clear();
    data_ = std::move(other.data_);
    size_ = other.size_;
    other.size_ = 0;
    return *this;
}

SensitiveValue &SensitiveValue::operator=(const char *str)
{
    Clear();
    SetData(str, std::strlen(str));
    return *this;
}

SensitiveValue &SensitiveValue::operator=(const std::string &str)
{
    Clear();
    SetData(str.data(), str.length());
    return *this;
}

bool SensitiveValue::Empty() const
{
    return data_ == nullptr || size_ == 0;
}

const char *SensitiveValue::GetData() const
{
    return Empty() ? "" : data_.get();
}

std::string SensitiveValue::GetMaskData() const
{
    if (!data_ || size_ == 0) {
        return "";
    }

    const std::string source(data_.get(), size_);
    const size_t hash = std::hash<std::string>{}(source);

    std::string output = std::to_string(hash);
    const size_t totalLen = output.length();
    if (totalLen == 0) {
        return "";
    }

    const size_t hiddenLen = (totalLen + 1) >> 1;
    for (size_t i = 0; i < hiddenLen; ++i) {
        output[i] = '*';
    }

    return output;
}

size_t SensitiveValue::GetSize() const
{
    return size_;
}

void SensitiveValue::SetData(const char *str, size_t size)
{
    if (str != nullptr && size > 0) {
        data_ = std::make_unique<char[]>(size + 1);
        size_ = size;
        int ret = memcpy_s(data_.get(), size_, str, size_);
        if (ret != EOK) {
            BUSLOG_WARN("memcpy failed, ret = {}", ret);
            Clear();
        }
    }
}
void SensitiveValue::Clear() noexcept
{
    if (data_ != nullptr && size_ > 0) {
        int ret = memset_s(data_.get(), size_, 0, size_);
        if (ret != EOK) {
            BUSLOG_WARN("memset failed, ret = {}", ret);
        }
    }
    size_ = 0;
    data_ = nullptr;
}

bool SensitiveValue::MoveTo(std::unique_ptr<char[]> &outData, size_t &outSize)
{
    if (Empty()) {
        return false;
    }
    outData = std::move(data_);
    outSize = size_;
    size_ = 0;
    return true;
}

bool SensitiveValue::operator==(const SensitiveValue &other) const
{
    if (size_ != other.size_) {
        return false;
    }

    if (size_ == 0) {
        return true;
    }

    if (data_ == nullptr || other.data_ == nullptr) {
        return false;
    }

    return strcmp(data_.get(), other.data_.get()) == 0;
}
}  // namespace litebus