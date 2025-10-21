/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#pragma once

#include "yr/parallel/parallel_for.h"
#include "yr/yr.h"
int AddOne(int x);

int PlusOne(int x);

int RaiseRuntimeError();

int Add(int x, int y);

int AddTwo(int x);

int AddAfterSleep(int x);

std::vector<char> BigBox(std::vector<char> bigParam);

int AddAfterSleepTen(int x);

int InvokeAndCancel_AddAfterSleepTen(int x);

int RemoteAdd(std::vector<YR::ObjectRef<int>> vec);

int SumVec(std::vector<YR::ObjectRef<int>> vec);

int KVGetPartKeysSuccess(bool x);

std::string InvokeReturnCustomEnvs(std::string key);

std::string ReturnCustomEnvs(std::string key);

int AfterSleepSec(int x);

extern const std::string execBigArgsError;
int ExecBigArgsAndFailed(std::vector<char> v);
std::string PutOneData();

class CounterB {
public:
    int count;
    std::mutex ctxIdSetMutex;
    std::unordered_set<size_t> ctxIdSet;
    CounterB(){};
    CounterB(int init)
    {
        count = init;
    }
    CounterB(const CounterB &c) {}
    CounterB &operator=(const CounterB &c) {}
    static CounterB *FactoryCreate(int init)
    {
        return new CounterB(init);
    }
    int GetCount();
    int Add(int x);
    int ParallelFor();
    size_t GetCtxIdsSize()
    {
        std::lock_guard<std::mutex> lock(ctxIdSetMutex);
        return ctxIdSet.size();
    }
    YR_STATE(count);
};

class Counter {
public:
    int count;
    int recoverFlag = 0;
    std::string key = "";

    std::vector<YR::NamedInstance<CounterB>> ranges;

    Counter() {}

    Counter(int init)
    {
        count = init;
    }

    static Counter *FactoryCreate(int init)
    {
        return new Counter(init);
    }

    int Add(int x);

    int AddRef(std::vector<YR::ObjectRef<int>> x);

    int Sleep();

    int SEGV();

    int Raise();

    int AddTwo(int x, int y);

    void Shutdown(uint64_t gracePeriodSecond);

    int AddRange(int max, int min, int step, bool sameLifecycle, int timeout, int getTimeout);

    std::string GetDir();

    int GetSigterm();

    int SaveState();

    int SaveGroupState();

    int LoadState();

    void Recover();

    int GetRecoverFlag();

    int GetGroupRecoverFlag();

    std::string returnActorEnvVar(std::string key);

    size_t GetPid();

    YR_STATE(key, count, ranges);
};

class CounterA {
public:
    int countA;
    int countB;
    YR::NamedInstance<CounterB> instance;

    CounterA(){};
    CounterA(int init)
    {
        countA = init;
        instance = YR::Instance(CounterB::FactoryCreate).Invoke(1);
    }

    static CounterA *FactoryCreate(int init)
    {
        return new CounterA(init);
    }

    int Add(int x);
    int GetCountB();
    int GetCountA();
    int TerminateB(int x);
    YR_STATE(countA, countB);
};

class CounterC {
public:
    int countC;
    int countA;
    YR::NamedInstance<CounterA> instanceA;

    CounterC(){};
    CounterC(int init)
    {
        countC = init;
        instanceA = YR::Instance(CounterA::FactoryCreate).Invoke(1);
    }

    static CounterC *FactoryCreate(int init)
    {
        return new CounterC(init);
    }
    int TerminateA(int x);
    int ChainTerminate();
    int Add(int x);
    int GetCountC();
    int GetCountA();
    int GetCountB();
    YR_STATE(countC, countA);
};

int ExcChain();
int Excdying();
int ExcfailMethod();
int ExcMethod();
int PlusOneFinalize(int x);
int ExcSIGILL();
int ExcSIGINT();
int ExcSIGSEGV();
int ExcSIGTERM();
int Sum(std::vector<int> a);
int SumWithObjectRef(std::vector<YR::ObjectRef<int>> a);
std::vector<YR::ObjectRef<int>> DirectReturn();

class Actor {
public:
    int count;
    Actor(){};
    Actor(int init)
    {
        count = init;
    }
    static Actor *FactoryCreate(int init)
    {
        return new Actor(init);
    }

    int get_sigill();
    int get_sigint();
    int get_sigsegv();
    int get_sigterm();
    YR_STATE(count);
};

int FailedForNTimesAndThenSuccess(int n);

class Adder {
public:
    Adder() = default;
    Adder(int initVal) : c_(initVal) {}
    ~Adder() = default;

    static Adder *FactoryCreate(int initVal)
    {
        return new Adder(initVal);
    }

    int Add(int val)
    {
        c_ += val;
        return c_;
    }

    int Get(void)
    {
        return c_;
    }

    YR_STATE(c_);

private:
    int c_;
};

class AdderProxy {
public:
    AdderProxy() = default;
    ~AdderProxy() = default;

    static AdderProxy *FactoryCreate()
    {
        return new AdderProxy();
    }

    void SetAdder(YR::NamedInstance<Adder> adder)
    {
        adder_ = adder;
    }

    void SetAdderByName(const std::string &name)
    {
        YR::InvokeOptions option;
        option.needOrder = true;
        adder_ = YR::Instance(Adder::FactoryCreate, name).Options(option).Invoke(10);
    }

    int Add(int val)
    {
        return *YR::Get(adder_.Function(&Adder::Add).Invoke(val));
    }

    YR_STATE(adder_);

private:
    YR::NamedInstance<Adder> adder_;
};

class CounterProxy {
  public:
    int count;
    CounterProxy() = default;
    CounterProxy(int init) { count = init; }
    ~CounterProxy() = default;

    static CounterProxy *FactoryCreate(int init) {
        return new CounterProxy(init);
    }
    int Add() { return count++; }
    int GetCounterAndInvoke(std::string &actorName)
    {
        auto counter = YR::GetInstance<Counter>(actorName, "", 60);
        auto objOne = counter.Function(&Counter::Add).Invoke(1);
        auto objTwo = counter.Function(&Counter::Add).Invoke(1);
        auto resOne = *YR::Get(objOne);
        auto resTwo = *YR::Get(objTwo);
        return resOne + resTwo;
    }
    YR_STATE(count);
};

int FunctionNotRegistered();

int FunctionRegistered();

int CallLocal(int x);
int CallCluster(int x);