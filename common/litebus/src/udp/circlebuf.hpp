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

#ifndef __LITEBUS_CIRCLEBUF_HPP__
#define __LITEBUS_CIRCLEBUF_HPP__

#include <vector>
#include <functional>

namespace litebus {

constexpr int DEFAULT_ARRAY_SIZE = 3;

template <typename T>
struct CircleArray {
    CircleArray(int size = DEFAULT_ARRAY_SIZE) : nextPos(0), maxSize(size), array(nullptr)
    {
        // default size is 3;
        if (size <= 0) {
            size = DEFAULT_ARRAY_SIZE;
        }

        array = new (std::nothrow) T *[size];
        if (array == nullptr) {
            return;
        }

        T *element = nullptr;
        for (int i = 0; i < size; ++i) {
            element = new (std::nothrow)(T);
            /* Just save the nullptr pointer when OOM, the user need to judge the return value of nextElement */
            array[i] = element;
        }
    }

    CircleArray &operator=(const CircleArray &that)
    {
        if (&that != this) {
            nextPos = that.nextPos;
            maxSize = that.maxSize;
            array = that.array;
        }
        return *this;
    }

    CircleArray(const CircleArray &init) : nextPos(init.nextPos), maxSize(init.maxSize), array(init.array){};

    ~CircleArray()
    {
        if (array == nullptr) {
            return;
        }
        for (int i = 0; i < maxSize; ++i) {
            delete array[i];
        }
        delete[] array;
    }

    /* The caller MUST judge the return value whether it is a nullptr pointer */
    T *const NextElement()
    {
        if (array == nullptr) {
            return nullptr;
        }

        if (nextPos == maxSize) {
            nextPos = 0;
        }
        return array[nextPos++];
    }

    void TraverseElements(const std::function<void(T *)> &function)
    {
        int pos;
        T *element = nullptr;

        if (array == nullptr) {
            return;
        }
        pos = nextPos;
        while (pos > 0) {
            element = array[--pos];
            if (element != nullptr) {
                function(element);
            }
        }

        pos = maxSize - 1;
        while (pos >= nextPos) {
            element = array[pos--];
            if (element != nullptr) {
                function(element);
            }
        }
    }

    int nextPos = 0; /* Next usable element's position */
    int maxSize; /* Max elements we can store */
    T **array;    /* Should not use vector since we need to keep the smallest memory size */
};
}    // namespace litebus

#endif
