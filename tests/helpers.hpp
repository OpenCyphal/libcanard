// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include "exposed.hpp"
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <unordered_map>

#if !(defined(CANARD_VERSION_MAJOR) && defined(CANARD_VERSION_MINOR))
#    error "Library version not defined"
#endif

#if !(defined(CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR) && defined(CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR))
#    error "UAVCAN specification version not defined"
#endif

namespace helpers
{
namespace dummy_allocator
{
inline auto allocate(CanardInstance* const ins, const std::size_t amount) -> void*
{
    (void) ins;
    (void) amount;
    return nullptr;
}

inline void free(CanardInstance* const ins, void* const pointer)
{
    (void) ins;
    (void) pointer;
}
}  // namespace dummy_allocator

/// We can't use the recommended true random std::random because it cannot be seeded by Catch2 (the testing framework).
template <typename T>
inline auto getRandomNatural(const T upper_open) -> T
{
    return static_cast<T>(static_cast<std::size_t>(std::rand()) % upper_open);  // NOLINT
}

/// An allocator that sits on top of the standard malloc() providing additional testing capabilities.
/// It allows the user to specify the maximum amount of memory that can be allocated; further requests will emulate OOM.
class TestAllocator
{
    mutable std::recursive_mutex           lock_;
    std::unordered_map<void*, std::size_t> allocated_;
    std::atomic<std::size_t>               ceiling_ = std::numeric_limits<std::size_t>::max();

public:
    TestAllocator()                      = default;
    TestAllocator(const TestAllocator&)  = delete;
    TestAllocator(const TestAllocator&&) = delete;
    auto operator=(const TestAllocator&) -> TestAllocator& = delete;
    auto operator=(const TestAllocator&&) -> TestAllocator& = delete;

    virtual ~TestAllocator()
    {
        std::unique_lock locker(lock_);
        for (auto [ptr, _] : allocated_)
        {
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            std::free(ptr);  // NOLINT
        }
    }

    [[nodiscard]] auto allocate(const std::size_t amount)
    {
        std::unique_lock locker(lock_);
        void*            p = nullptr;
        if ((amount > 0U) && ((getTotalAllocatedAmount() + amount) <= ceiling_))
        {
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            p = std::malloc(amount);  // NOLINT
            if (p == nullptr)
            {
                throw std::bad_alloc();  // This is a test suite failure, not a failed test. Mind the difference.
            }
            // Random-fill the memory to make sure no assumptions are made about its contents.
            std::uniform_int_distribution<std::uint16_t> dist(0, 255U);
            std::generate_n(reinterpret_cast<std::byte*>(p), amount, [&]() {
                return static_cast<std::byte>(getRandomNatural(256U));
            });
            allocated_.emplace(p, amount);
        }
        return p;
    }

    void deallocate(void* const pointer)
    {
        if (pointer != nullptr)
        {
            std::unique_lock locker(lock_);
            const auto       it = allocated_.find(pointer);
            REQUIRE(it != std::end(allocated_));  // Catch an attempt to deallocate memory that is not allocated.
            // Damage the memory to make sure it's not used after deallocation.
            std::uniform_int_distribution<std::uint16_t> dist(0, 255U);
            std::generate_n(reinterpret_cast<std::byte*>(pointer), it->second, [&]() {
                return static_cast<std::byte>(getRandomNatural(256U));
            });
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            std::free(it->first);  // NOLINT
            allocated_.erase(it);
        }
    }

    [[nodiscard]] auto getNumAllocatedFragments() const
    {
        std::unique_lock locker(lock_);
        return std::size(allocated_);
    }

    [[nodiscard]] auto getTotalAllocatedAmount() const -> std::size_t
    {
        std::unique_lock locker(lock_);
        std::size_t      out = 0U;
        for (auto [_, size] : allocated_)
        {
            out += size;
        }
        return out;
    }

    [[nodiscard]] auto getAllocationCeiling() const { return static_cast<std::size_t>(ceiling_); }
    void               setAllocationCeiling(const std::size_t amount) { ceiling_ = amount; }
};

/// An enhancing wrapper over the library to remove boilerplate from the tests.
class Instance
{
    CanardInstance canard_ = canardInit(&Instance::trampolineAllocate, &Instance::trampolineDeallocate);
    TestAllocator  allocator_;

    static auto trampolineAllocate(CanardInstance* const ins, const std::size_t amount) -> void*
    {
        auto* p = reinterpret_cast<Instance*>(ins->user_reference);
        return p->allocator_.allocate(amount);
    }

    static void trampolineDeallocate(CanardInstance* const ins, void* const pointer)
    {
        auto* p = reinterpret_cast<Instance*>(ins->user_reference);
        p->allocator_.deallocate(pointer);
    }

public:
    Instance() { canard_.user_reference = this; }

    virtual ~Instance() = default;

    Instance(const Instance&)  = delete;
    Instance(const Instance&&) = delete;
    auto operator=(const Instance&) -> Instance& = delete;
    auto operator=(const Instance&&) -> Instance& = delete;

    [[nodiscard]] auto rxAccept(const CanardFrame&           frame,
                                const uint8_t                redundant_transport_index,
                                CanardRxTransfer&            out_transfer,
                                CanardRxSubscription** const out_subscription)
    {
        return canardRxAccept(&canard_, &frame, redundant_transport_index, &out_transfer, out_subscription);
    }

    [[nodiscard]] auto rxSubscribe(const CanardTransferKind transfer_kind,
                                   const CanardPortID       port_id,
                                   const std::size_t        extent,
                                   const CanardMicrosecond  transfer_id_timeout_usec,
                                   CanardRxSubscription&    out_subscription)
    {
        return canardRxSubscribe(&canard_, transfer_kind, port_id, extent, transfer_id_timeout_usec, &out_subscription);
    }

    [[nodiscard]] auto rxUnsubscribe(const CanardTransferKind transfer_kind, const CanardPortID port_id)
    {
        return canardRxUnsubscribe(&canard_, transfer_kind, port_id);
    }

    [[nodiscard]] auto getNodeID() const { return canard_.node_id; }
    void               setNodeID(const std::uint8_t x) { canard_.node_id = x; }

    [[nodiscard]] auto getMTU() const { return canard_.mtu_bytes; }
    void               setMTU(const std::size_t x) { canard_.mtu_bytes = x; }

    [[nodiscard]] auto getAllocator() -> TestAllocator& { return allocator_; }

    [[nodiscard]] auto getInstance() -> CanardInstance& { return canard_; }
    [[nodiscard]] auto getInstance() const -> const CanardInstance& { return canard_; }
};

class TxQueue
{
    CanardTxQueue que_;

public:
    explicit TxQueue(const std::size_t capacity) : que_(canardTxInit(capacity))
    {
        REQUIRE(que_.user_reference == nullptr);  // Ensure that the initialization is correct.
        que_.user_reference = this;               // This is simply to ensure it is not overwritten unexpectedly.
    }
    virtual ~TxQueue() = default;

    TxQueue(const TxQueue&) = delete;
    TxQueue(TxQueue&&)      = delete;
    auto operator=(const TxQueue&) -> TxQueue& = delete;
    auto operator=(TxQueue&&) -> TxQueue& = delete;

    [[nodiscard]] auto push(CanardInstance* const         ins,
                            const CanardMicrosecond       transmission_deadline_usec,
                            const CanardTransferMetadata& metadata,
                            const size_t                  payload_size,
                            const void* const             payload)
    {
        REQUIRE(this == que_.user_reference);  // Ensure not overwritten.
        return canardTxPush(&que_, ins, transmission_deadline_usec, &metadata, payload_size, payload);
    }

    [[nodiscard]] auto peek() const -> const CanardFrame* { return canardTxPeek(&que_); }

    [[nodiscard]] auto pop() -> CanardFrame*
    {
        REQUIRE(this == que_.user_reference);  // Ensure not overwritten.
        const auto* pk  = peek();
        auto*       out = canardTxPop(&que_);
        REQUIRE(pk == out);
        return out;
    }

    [[nodiscard]] auto getRoot() const { return reinterpret_cast<const exposed::TxQueueItem*>(que_.head); }

    [[nodiscard]] auto getSize() const
    {
        REQUIRE(this == que_.user_reference);  // Ensure not overwritten.
        std::size_t out = 0U;
        const auto* p   = getRoot();
        while (p != nullptr)
        {
            ++out;
            p = p->next;
        }
        REQUIRE(que_.size == out);
        return out;
    }

    [[nodiscard]] auto getInstance() -> CanardTxQueue& { return que_; }
    [[nodiscard]] auto getInstance() const -> const CanardTxQueue& { return que_; }
};

}  // namespace helpers
