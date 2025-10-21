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

#include <cstdlib>

#include "actor/buslog.hpp"
#include "actor/sysmgr_actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/iomgr.hpp"

#ifdef HTTP_ENABLED
#include "httpd/http_iomgr.hpp"
#include "httpd/http_client.hpp"
#endif

#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"
#endif

#include "tcp/tcpmgr.hpp"
#include "timer/timertools.hpp"
#include "utils/os_utils.hpp"
#include "litebus.hpp"
#ifdef UDP_ENABLED
#include "udp/udpmgr.hpp"
#endif
#include "litebus.h"

extern "C" {
int LitebusInitializeC(const struct LitebusConfig *config)
{
    if (config == nullptr) {
        return -1;
    }

    if (config->threadCount == 0) {
        return -1;
    }

    if (config->httpKmsgFlag != 0 && config->httpKmsgFlag != 1) {
        return -1;
    }
    litebus::SetHttpKmsgFlag(config->httpKmsgFlag);

    return litebus::Initialize(std::string(config->tcpUrl), std::string(config->tcpUrlAdv),
                               std::string(config->udpUrl), std::string(config->udpUrlAdv), config->threadCount);
}

void LitebusFinalizeC()
{
    litebus::Finalize();
}
}

namespace litebus {
constexpr auto LITEBUSTHREADMIN = 3;
constexpr auto LITEBUSTHREADMAX = 100;
constexpr auto LITEBUSTHREADS = 10;

constexpr auto SYSMGR_TIMER_DURATION = 600000;

namespace local {

static LitebusAddress *g_litebusAddress = new (std::nothrow) LitebusAddress();
static std::atomic_bool g_finalizeLitebusStatus(false);

}    // namespace local

const LitebusAddress &GetLitebusAddress()
{
    BUS_OOM_EXIT(local::g_litebusAddress);
    return *local::g_litebusAddress;
}

bool SetServerIo(std::shared_ptr<litebus::IOMgr> &io, std::string &advertiseUrl, const std::string protocol,
                 const std::string &url)
{
    if (protocol == "tcp") {
        size_t index = advertiseUrl.find("://");
        if (index != std::string::npos) {
            advertiseUrl = advertiseUrl.substr(index + URL_PROTOCOL_IP_SEPARATOR.size());
        }
        BUSLOG_INFO("create tcp iomgr. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        if (local::g_litebusAddress == nullptr) {
            BUSLOG_ERROR("Couldn't allocate memory for LitebusAddress");
            return false;
        }
        local::g_litebusAddress->scheme = protocol;
        local::g_litebusAddress->ip = AID("test@" + advertiseUrl).GetIp();
        local::g_litebusAddress->port = AID("test@" + advertiseUrl).GetPort();

#ifdef HTTP_ENABLED
        litebus::HttpIOMgr::EnableHttp();
#endif
        io.reset(new (std::nothrow) litebus::TCPMgr());
        if (io == nullptr) {
            BUSLOG_ERROR("Couldn't allocate memory for TCPMgr");
            return false;
        }
    }
#ifdef UDP_ENABLED
    else if (protocol == "udp") {
        BUSLOG_INFO("create udp iomgr. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        io.reset(new (std::nothrow) litebus::UDPMgr());
        if (io == nullptr) {
            BUSLOG_ERROR("Couldn't allocate memory for UDPMgr");
            return false;
        }
    }
#endif
    else {
        BUSLOG_INFO("unsupport protocol. {}", protocol.c_str());
        return false;
    }

    return true;
}

static int StartServer(const std::string &url, const std::string &advUrl, IOMgr::MsgHandler handle)
{
    std::string protocol = "tcp";
    std::shared_ptr<litebus::IOMgr> io = nullptr;
    std::string advertiseUrl = advUrl;

    if (AID("test@" + url).OK() == false) {
        BUSLOG_ERROR("URL is error. Url={},advertiseUrl={}", url.c_str(), advUrl.c_str());
        return BUS_ERROR;
    }

    if (advertiseUrl == "") {
        advertiseUrl = url;
    }
    if (AID("test@" + advertiseUrl).OK() == false) {
        BUSLOG_ERROR("URL is error. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        return BUS_ERROR;
    }

    size_t index = url.find("://");
    if (index != std::string::npos) {
        protocol = url.substr(0, index);
    }

    std::shared_ptr<litebus::IOMgr> ioMgrRef = ActorMgr::GetIOMgrRef(protocol);
    if (ioMgrRef != nullptr) {
        BUSLOG_ERROR("protocol is exist. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        return BUS_OK;
    }

    if (!SetServerIo(io, advertiseUrl, protocol, url)) {
        return BUS_ERROR;
    }

    bool ok = io->Init();
    if (!ok) {
        BUSLOG_ERROR("io init failed. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        return BUS_ERROR;
    }

    io->RegisterMsgHandle(handle);
    ActorMgr::GetActorMgrRef()->AddUrl(protocol, advertiseUrl);
    ActorMgr::GetActorMgrRef()->AddIOMgr(protocol, io);

    ok = io->StartIOServer(url, advertiseUrl);
    if (!ok) {
        BUSLOG_ERROR("server start failed. Url={},advertiseUrl={}", url.c_str(), advertiseUrl.c_str());
        return BUS_ERROR;
    }

    return BUS_OK;
}

void SetThreadCount(int threadCount)
{
    int tmpThreadCount = LITEBUSTHREADS;
    if (threadCount == 0) {
        Option<std::string> sThreadCount = os::GetEnv("LITEBUS_THREADS");
        if (sThreadCount.IsSome()) {
            try {
                tmpThreadCount = std::stoi(sThreadCount.Get().c_str());
            } catch (std::exception &e) {
                BUSLOG_ERROR("failed to convert the thread count to a number, use default value, error: {}", e.what());
            }
        }
    } else {
        tmpThreadCount = threadCount;
    }

    if (tmpThreadCount < LITEBUSTHREADMIN || tmpThreadCount > LITEBUSTHREADMAX) {
        tmpThreadCount = LITEBUSTHREADS;
    }

    BUSLOG_INFO("litebus thread count is:{}", tmpThreadCount);
    ActorMgr::GetActorMgrRef()->Initialize(tmpThreadCount);
}

class LiteBusExit {
public:
    LiteBusExit()
    {
        BUSLOG_INFO("trace: enter LiteBusExit()---------");
    }
    ~LiteBusExit()
    {
        try {
            BUSLOG_INFO("trace: enter ~LiteBusExit()---------");
            litebus::Finalize();
        } catch (...) {
            // Ignore
        }
    }
};

int InitializeImp(const std::string &tcpUrl, const std::string &tcpUrlAdv, const std::string &udpUrl,
                  const std::string &udpUrlAdv, int threadCount)
{
    BUSLOG_INFO("litebus starts ......");
    (void)signal(SIGPIPE, SIG_IGN);
#ifdef SSL_ENABLED
    bool sslInitialized = litebus::openssl::SslInit();
    if (!sslInitialized) {
        BUSLOG_ERROR("ssl initialize failed");
        return BUS_ERROR;
    }
#endif

    if (!TimerTools::Initialize()) {
        BUSLOG_ERROR("Failed to initialize timer tools");
        return BUS_ERROR;
    }

    // start actor's thread
    SetThreadCount(threadCount);

#ifdef HTTP_ENABLED
    if (!litebus::http::HttpClient::GetInstance()->Initialize()) {
        BUSLOG_ERROR("http client initialize failed");
        return BUS_ERROR;
    }
#endif

#ifdef UDP_ENABLED
    if (!udpUrl.empty()) {
        BUSLOG_INFO("start IOMgr with. Url={},advertiseUrl={}", udpUrl.c_str(), udpUrlAdv.c_str());
        auto result = StartServer(udpUrl, udpUrlAdv, &ActorMgr::Receive);
        if (result != BUS_OK) {
            return result;
        }
    }
#endif

    if (!tcpUrl.empty()) {
        BUSLOG_INFO("start IOMgr with. Url={},advertiseUrl={}", tcpUrl.c_str(), tcpUrlAdv.c_str());
        auto result = StartServer(tcpUrl, tcpUrlAdv, &ActorMgr::Receive);
        if (result != BUS_OK) {
            return result;
        }
    }

    (void)litebus::Spawn(std::make_shared<SysMgrActor>(SYSMGR_ACTOR_NAME, SYSMGR_TIMER_DURATION));

    BUSLOG_INFO("litebus has started.");
    return BUS_OK;
}

int Initialize(const std::string &tcpUrl, const std::string &tcpUrlAdv, const std::string &udpUrl,
               const std::string &udpUrlAdv, int threadCount)
{
    static std::atomic_bool initLitebusStatus(false);
    bool inite = false;
    if (initLitebusStatus.compare_exchange_strong(inite, true) == false) {
        BUSLOG_INFO("litebus has been initialized");
        return BUS_OK;
    }

    int result = BUS_OK;

    do {
        try {
            result = InitializeImp(tcpUrl, tcpUrlAdv, udpUrl, udpUrlAdv, threadCount);
        } catch (const std::exception &e) {
            BUSLOG_ERROR("Litebus catch exception, what={}", e.what());
            result = BUS_ERROR;
        }
    } while (0);

    static LiteBusExit busExit;

    return result;
}

AID Spawn(ActorReference actor, bool sharedThread, bool start)
{
    if (actor == nullptr) {
        BUSLOG_FATAL("Actor is nullptr.");
    }

    if (local::g_finalizeLitebusStatus.load() == true) {
        return actor->GetAID();
    } else {
        return ActorMgr::GetActorMgrRef()->Spawn(actor, sharedThread, start);
    }
}
void SetActorStatus(const AID &actor, bool start)
{
    ActorMgr::GetActorMgrRef()->SetActorStatus(actor, start);
}

void Await(const ActorReference &actor)
{
    ActorMgr::GetActorMgrRef()->Wait(actor->GetAID());
}

void Await(const AID &actor)
{
    ActorMgr::GetActorMgrRef()->Wait(actor);
}

// brief get actor with aid
ActorReference GetActor(const AID &actor)
{
    return ActorMgr::GetActorMgrRef()->GetActor(actor);
}

void Terminate(const AID &actor)
{
    ActorMgr::GetActorMgrRef()->Terminate(actor);
}

void TerminateAll()
{
    litebus::ActorMgr::GetActorMgrRef()->TerminateAll();
}

void Finalize()
{
    bool inite = false;
    if (local::g_finalizeLitebusStatus.compare_exchange_strong(inite, true) == false) {
        BUSLOG_INFO("litebus has been Finalized.");
        return;
    }

    BUSLOG_INFO("litebus starts to finalize.");
    litebus::ActorMgr::GetActorMgrRef()->Finalize();
    TimerTools::Finalize();

    BUSLOG_INFO("litebus has been finalized.");
}

void SetDelegate(const std::string &delegate)
{
    litebus::ActorMgr::GetActorMgrRef()->SetDelegate(delegate);
}

static int g_httpKmsgEnable = -1;
void SetHttpKmsgFlag(int flag)
{
    BUSLOG_INFO("Set LiteBus http message format: {}", flag);
    g_httpKmsgEnable = flag;
}

int GetHttpKmsgFlag()
{
    return g_httpKmsgEnable;
}

}    // namespace litebus
