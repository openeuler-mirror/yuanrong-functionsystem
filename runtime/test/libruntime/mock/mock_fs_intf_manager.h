#pragma once

#include "gmock/gmock.h"
#include "src/libruntime/fsclient/fs_intf_manager.h"

namespace YR {
namespace test {
using YR::Libruntime::FSIntfReaderWriter;
using YR::Libruntime::ReaderWriterClientOption;
using YR::Libruntime::ProtocolType;
class MockFSIntfManager : public YR::Libruntime::FSIntfManager {
public:
    MockFSIntfManager() : YR::Libruntime::FSIntfManager(nullptr) {}
    ~MockFSIntfManager() = default;
    MOCK_METHOD(std::shared_ptr<FSIntfReaderWriter>, NewFsIntfClient,
                (const std::string &srcInstance, const std::string &dstInstance, const std::string &runtimeID,
                 const ReaderWriterClientOption &option, ProtocolType type),
                (override));
    MOCK_METHOD(std::shared_ptr<FSIntfReaderWriter>, TryGet, (const std::string &instanceID), (override));
    MOCK_METHOD(std::shared_ptr<FSIntfReaderWriter>, Get, (const std::string &instanceID), (override));
};
}  // namespace test
}  // namespace YR