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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_STREAM_WRITER_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_STREAM_WRITER_H

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "common/proto/pb/posix/exec_service.grpc.pb.h"

namespace functionsystem {

using exec_service::ExecMessage;
using ::grpc::ServerReaderWriter;

/**
 * StreamWriter provides thread-safe gRPC stream writing functionality.
 *
 * Uses a queue and a dedicated writer thread to serialize all write operations,
 * solving the issue that gRPC ServerReaderWriter::Write() is not thread-safe.
 *
 * Usage:
 *   auto writer = std::make_shared<StreamWriter>(stream);
 *   // Call from any thread
 *   writer->Enqueue(message);  // Thread-safe
 *   // When done
 *   writer->Stop();
 */
class StreamWriter : public std::enable_shared_from_this<StreamWriter> {
public:
    /**
     * Constructor
     * @param stream gRPC bidirectional stream pointer
     */
    explicit StreamWriter(ServerReaderWriter<ExecMessage, ExecMessage> *stream);

    /**
     * Destructor, automatically calls Stop()
     */
    ~StreamWriter();

    // Disable copy
    StreamWriter(const StreamWriter &) = delete;
    StreamWriter &operator=(const StreamWriter &) = delete;

    /**
     * Enqueue a message for sending (thread-safe)
     * @param msg Message to send
     * @return false if writer is stopped, otherwise true
     */
    bool Enqueue(ExecMessage msg);

    /**
     * Stop the writer, wait for all messages in queue to be sent
     */
    void Stop();

    /**
     * Stop immediately, discard unsent messages in queue
     */
    void ForceStop();

    /**
     * Check if running
     */
    bool IsRunning() const
    {
        return running_.load();
    }

    /**
     * Get the number of pending messages in queue
     */
    size_t GetPendingCount() const;

private:
    /**
     * Writer thread main loop
     */
    void WriteLoop();

private:
    ServerReaderWriter<ExecMessage, ExecMessage> *stream_;

    std::queue<ExecMessage> writeQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable cv_;

    std::atomic<bool> running_{ true };
    std::atomic<bool> forceStop_{ false };

    std::thread writerThread_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_STREAM_WRITER_H
