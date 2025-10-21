/*
 * uuid_tests.cpp
 *
 *  Created on: 2018-12-11
 *      Author:
 */
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include "async/uuid_base.hpp"
#include "async/uuid_generator.hpp"
#include <climits>

using litebus::Option;
using litebus::uuid_generator::UUID;
using litebus::uuids::RandomBasedGenerator;
using litebus::uuids::uuid;
TEST(UUIDTest, TestRandom)
{
    uuid u = RandomBasedGenerator::GenerateRandomUuid();

    EXPECT_FALSE(u.IsNilUUID());
    EXPECT_EQ((size_t)16, u.Size());
}
TEST(UUIDTest, TestRandomUnique)
{
    std::map<std::string, int> genMap;
    std::multimap<std::string, int> resultMap;
    for (int i = 0; i < 100000; ++i) {
        std::string key = UUID::GetRandomUUID().ToString();

        if (genMap.find(key) != genMap.end()) {
            resultMap.insert(std::pair<std::string, int>(key, 1));
        }
        genMap[key]++;
    }

    for (auto it = resultMap.begin(); it != resultMap.end(); ++it) {
        std::cout << " Key= " << it->first << " count=" << it->second << std::endl;
    }

    EXPECT_TRUE(resultMap.empty());
}

TEST(UUIDTest, NilUUID)
{
    uuid u;

    EXPECT_FALSE(u.IsNilUUID());
}

TEST(UUIDTest, GetUUIDString)
{
    uuid uuid1 = RandomBasedGenerator::GenerateRandomUuid();
    EXPECT_FALSE(uuid1.IsNilUUID());
    EXPECT_EQ((size_t)16, uuid1.Size());

    uuid uuid2 = RandomBasedGenerator::GenerateRandomUuid();
    EXPECT_FALSE(uuid2.IsNilUUID());
    EXPECT_EQ((size_t)16, uuid2.Size());

    uuid uuid3;
    std::string uuidstring = uuid3.ToBytes(uuid2);
    Option<uuid> oUUID = uuid3.FromBytes(uuidstring);
    ASSERT_TRUE(oUUID.IsSome());
    uuid3 = oUUID.Get();
    EXPECT_FALSE(uuid3.IsNilUUID());
    EXPECT_EQ((size_t)16, uuid3.Size());
    EXPECT_TRUE(uuid2 == uuid3);

    UUID UUID1 = UUID1.GetRandomUUID();
    std::string UUID1Str = UUID1.ToString();
    BUSLOG_INFO("UUID1： {}", UUID1Str);
    Option<uuid> ouuid4 = uuid::FromString(UUID1Str);
    ASSERT_TRUE(ouuid4.IsSome());
    UUID UUID2(ouuid4.Get());
    EXPECT_EQ(UUID1Str, UUID2.ToString());
    EXPECT_EQ(UUID1.ToBytes(UUID1), UUID1.ToBytes(UUID2));

    EXPECT_FALSE(uuid1 == uuid2);
}

TEST(UUIDTest, GetTest)
{
    uuid u = RandomBasedGenerator::GenerateRandomUuid();
    auto res = u.Get();

    EXPECT_TRUE(res != nullptr);
}

#ifdef HTTP_ENABLED
TEST(UUIDTest, HttpClientConnIdTest)
{
    int id = 0;

    while (1) {
        id = litebus::localid_generator::GenHttpClientConnId();
        if (id == INT_MAX - 1) {
            break;
        }
    }
    BUSLOG_INFO("id： {}", id);
    id = litebus::localid_generator::GenHttpClientConnId();
    EXPECT_TRUE(id == 1);
    id = litebus::localid_generator::GenHttpClientConnId();
    EXPECT_TRUE(id == 2);
}

TEST(UUIDTest, HttpServerConnIdTest)
{
    int id = 0;

    while (1) {
        id = litebus::localid_generator::GenHttpServerConnId();
        if (id == INT_MAX - 1) {
            break;
        }
    }
    BUSLOG_INFO("id： {}", id);
    id = litebus::localid_generator::GenHttpServerConnId();
    EXPECT_TRUE(id == 1);
    id = litebus::localid_generator::GenHttpServerConnId();
    EXPECT_TRUE(id == 2);
}
#endif
