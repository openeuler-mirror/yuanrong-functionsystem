#pragma once

#include "gmock/gmock.h"
#include "src/libruntime/fsclient/fs_intf_reader_writer.h"

namespace YR {
namespace test {
using YR::Libruntime::FSIntfReaderWriter;
using YR::Libruntime::ReaderWriterClientOption;
using YR::Libruntime::StreamingMessage;
using YR::Libruntime::ErrorInfo;

class MockFSIntfReaderWriter : public YR::Libruntime::FSIntfReaderWriter {
public:
    MockFSIntfReaderWriter() : YR::Libruntime::FSIntfReaderWriter("", "", "") {}
    ~MockFSIntfReaderWriter() = default;
    MOCK_METHOD(void, Write,
                (const std::shared_ptr<StreamingMessage> &msg,
                 std::function<void(bool, ErrorInfo)> callback,
                 std::function<void(bool)> preWrite),
                (override));
    MOCK_METHOD(ErrorInfo, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(bool, Available, (), (override));
    MOCK_METHOD(bool, Abnormal, (), (override));
};
}  // namespace test
}  // namespace YR