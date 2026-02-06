/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "stream_writer.h"

#include "common/logs/logging.h"

namespace functionsystem {

StreamWriter::StreamWriter(ServerReaderWriter<ExecMessage, ExecMessage>* stream)
    : stream_(stream)
{
    YRLOG_DEBUG("StreamWriter created");
    writerThread_ = std::thread(&StreamWriter::WriteLoop, this);
}

StreamWriter::~StreamWriter()
{
    Stop();
    YRLOG_DEBUG("StreamWriter destroyed");
}

bool StreamWriter::Enqueue(ExecMessage msg)
{
    if (!running_.load()) {
        YRLOG_WARN("StreamWriter is stopped, message dropped for session_id: {}",
                  msg.session_id());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        size_t queueSize = writeQueue_.size();
        writeQueue_.push(std::move(msg));
        YRLOG_DEBUG("Message enqueued, session_id: {}, queue_size: {} -> {}",
                   msg.session_id(), queueSize, writeQueue_.size());
    }
    cv_.notify_one();

    return true;
}

void StreamWriter::Stop()
{
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }

    YRLOG_DEBUG("StreamWriter stopping, waiting for queue to drain");

    // Wake up writer thread
    cv_.notify_one();

    // Wait for thread to finish
    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    YRLOG_DEBUG("StreamWriter stopped");
}

void StreamWriter::ForceStop()
{
    forceStop_ = true;
    Stop();
}

size_t StreamWriter::GetPendingCount() const
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    return writeQueue_.size();
}

void StreamWriter::WriteLoop()
{
    YRLOG_DEBUG("StreamWriter write loop started");
    while (true) {
        ExecMessage msg;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            // Wait: has message OR stop signal
            cv_.wait(lock, [this] {
                return !writeQueue_.empty() || !running_.load();
            });

            // If force stop, exit immediately
            if (forceStop_.load()) {
                YRLOG_DEBUG("StreamWriter force stopped, {} messages dropped",
                           writeQueue_.size());
                break;
            }

            // If queue is empty and stopped, exit
            if (writeQueue_.empty() && !running_.load()) {
                break;
            }

            // If queue is empty but still running, continue waiting
            if (writeQueue_.empty()) {
                continue;
            }

            // Take one message from queue
            msg = std::move(writeQueue_.front());
            writeQueue_.pop();
        }

        // Execute write operation outside lock
        const std::string& sessionId = msg.session_id();
        YRLOG_DEBUG("Writing message to stream, session_id: {}, has_output_data: {}, has_status: {}",
                   sessionId,
                   msg.has_output_data(),
                   msg.has_status());

        if (!stream_->Write(msg)) {
            YRLOG_ERROR("StreamWriter failed to write message, session_id: {}",
                      sessionId);

            // Write failed, possibly stream closed
            // Based on strategy, decide whether to continue
            // Here we choose to continue trying to send remaining messages
        } else {
            YRLOG_DEBUG("Message written successfully, session_id: {}", sessionId);
        }
    }

    YRLOG_DEBUG("StreamWriter write loop exited");
}

}  // namespace functionsystem
