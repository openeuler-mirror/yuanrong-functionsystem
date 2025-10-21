#include <gmock/gmock.h>
#include <gtest/gtest.h>
#define private public
#include "actor/sysmgr_actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/iomgr.hpp"

using namespace std;

namespace litebus {

class SysMgrActorTest : public ::testing::Test {
protected:
    void SetUp()
    {
    }

    void TearDown()
    {
    }
};

TEST_F(SysMgrActorTest, SendMetricsDurationCallback)
{
    // create obj
    SysMgrActor *actorPtr = new (std::nothrow) SysMgrActor("name", 1);
    EXPECT_TRUE(actorPtr != nullptr);

    // send
    actorPtr->SendMetricsDurationCallback();
    delete actorPtr;
    actorPtr = nullptr;
}

TEST_F(SysMgrActorTest, SendMetricsDurationCallback02)
{
    // create obj
    SysMgrActor *actorPtr = new (std::nothrow) SysMgrActor("name", 1);
    EXPECT_TRUE(actorPtr != nullptr);
    actorPtr->printSendMetricsDuration = 0;

    // send
    actorPtr->SendMetricsDurationCallback();
    delete actorPtr;
    actorPtr = nullptr;
}
TEST_F(SysMgrActorTest, HandleSendMetricsCallback)
{
    // create obj
    SysMgrActor *actorPtr = new (std::nothrow) SysMgrActor("name", 1);
    EXPECT_TRUE(actorPtr != nullptr);

    // make msg
    std::string apiServerUrl("127.0.0.1:2227");
    std::string localUrl("127.0.0.1:8080");
    AID tofrom("apiServerName", apiServerUrl);
    AID from("testserver", localUrl);
    IntTypeMetrics intMetrics;
    StringTypeMetrics stringMetrics;
    std::unique_ptr<MetricsMessage> message(
        new MetricsMessage(tofrom, SYSMGR_ACTOR_NAME, METRICS_SEND_MSGNAME, intMetrics, stringMetrics));
    // handle send
    actorPtr->HandleSendMetricsCallback(from, std::move(message));
    delete actorPtr;
    actorPtr = nullptr;
}

TEST_F(SysMgrActorTest, PrintMetrics)
{
    // create obj
    IntTypeMetrics intMetrics;
    StringTypeMetrics stringMetrics;
    std::string apiServerUrl("27.0.0.1:2227");
    AID tofrom("apiServerName", apiServerUrl);
    MetricsMessage *msgPtr =
        new (std::nothrow) MetricsMessage(tofrom, SYSMGR_ACTOR_NAME, METRICS_SEND_MSGNAME, intMetrics, stringMetrics);
    EXPECT_TRUE(msgPtr != nullptr);
    msgPtr->PrintMetrics();
    delete msgPtr;
    msgPtr = nullptr;
}
}
