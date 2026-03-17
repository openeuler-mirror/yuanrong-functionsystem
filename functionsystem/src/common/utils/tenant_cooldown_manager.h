/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#ifndef COMMON_UTILS_TENANT_COOLDOWN_MANAGER_H
#define COMMON_UTILS_TENANT_COOLDOWN_MANAGER_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include <async/asyncafter.hpp>

namespace functionsystem {

/**
 * Thread-unsafe per-tenant quota cooldown manager.
 * Must only be accessed on the owning actor's thread.
 *
 * Fixes timer-cancellation race: each Apply() call increments a per-tenant
 * generation counter. The expiry callback captures that generation and calls
 * OnExpired(tenantID, generation). OnExpired() only removes the block when the
 * stored generation still matches, so a stale queued callback cannot clear a
 * newer cooldown.
 *
 * Typical actor usage:
 *   // In OnTenantQuotaExceeded:
 *   cooldownMgr_.Apply(tenantID, cooldownMs, [&](uint64_t gen) {
 *       return litebus::AsyncAfter(cooldownMs, GetAID(),
 *                                  &MyActor::OnTenantCooldownExpired, tenantID, gen);
 *   });
 *
 *   // In OnTenantCooldownExpired(std::string tenantID, uint64_t generation):
 *   cooldownMgr_.OnExpired(tenantID, generation);
 */
class TenantCooldownManager {
public:
    /**
     * Apply or reset cooldown for a tenant.
     * Cancels any existing timer, increments the generation, then calls
     * scheduleTimer(newGeneration) to let the caller schedule AsyncAfter.
     * No-op if tenantID is empty.
     *
     * @param tenantID      Tenant to block. Must not be empty.
     * @param scheduleTimer Callable(uint64_t generation) → litebus::Timer.
     *                      The caller should call litebus::AsyncAfter inside it,
     *                      capturing the provided generation for the expiry handler.
     */
    void Apply(const std::string &tenantID, const std::function<litebus::Timer(uint64_t)> &scheduleTimer)
    {
        if (tenantID.empty()) {
            return;
        }
        auto &entry = entries_[tenantID];
        (void)litebus::TimerTools::Cancel(entry.timer);
        ++entry.generation;
        entry.timer = scheduleTimer(entry.generation);
    }

    /** Returns true if the tenant is currently blocked. */
    bool IsBlocked(const std::string &tenantID) const
    {
        return entries_.count(tenantID) > 0;
    }

    /**
     * Call from the actor's expiry handler.
     * Only unblocks if @p generation still matches the stored value,
     * preventing stale callbacks from clearing a newer cooldown.
     * @return true if the entry was actually removed (real expiry),
     *         false if the generation was stale (suppressed).
     */
    bool OnExpired(const std::string &tenantID, uint64_t generation)
    {
        auto it = entries_.find(tenantID);
        if (it != entries_.end() && it->second.generation == generation) {
            entries_.erase(it);
            return true;
        }
        return false;
    }

    /** Cancel all pending timers. Call during actor shutdown. */
    void CancelAll()
    {
        for (auto &[id, entry] : entries_) {
            (void)litebus::TimerTools::Cancel(entry.timer);
        }
        entries_.clear();
    }

    ~TenantCooldownManager()
    {
        CancelAll();
    }

private:
    struct Entry {
        litebus::Timer timer;
        uint64_t generation{0};
    };
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace functionsystem

#endif  // COMMON_UTILS_TENANT_COOLDOWN_MANAGER_H
