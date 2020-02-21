// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include "exposed.hpp"
#include <algorithm>
#include <cstdarg>
#include <numeric>
#include <random>
#include <unordered_map>

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

/// An allocator that sits on top of the standard malloc() providing additional testing capabilities.
/// It allows the user to specify the maximum amount of memory that can be allocated; further requests will emulate OOM.
class TestAllocator
{
    std::unordered_map<void*, std::size_t> allocated_;
    std::size_t                            ceiling_ = std::numeric_limits<std::size_t>::max();

    static auto getRandomByte()
    {
        static std::random_device                           rd;
        static std::mt19937                                 gen(rd());
        static std::uniform_int_distribution<std::uint16_t> dis(0, 255U);
        return static_cast<std::byte>(dis(gen));
    }

public:
    TestAllocator()                      = default;
    TestAllocator(const TestAllocator&)  = delete;
    TestAllocator(const TestAllocator&&) = delete;
    auto operator=(const TestAllocator&) -> TestAllocator& = delete;
    auto operator=(const TestAllocator &&) -> TestAllocator& = delete;

    virtual ~TestAllocator()
    {
        for (auto [ptr, _] : allocated_)
        {
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            std::free(ptr);  // NOLINT
        }
    }

    [[nodiscard]] auto allocate(const std::size_t amount)
    {
        void* p = nullptr;
        if ((amount > 0U) && ((getTotalAllocatedAmount() + amount) <= ceiling_))
        {
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            p = std::malloc(amount);  // NOLINT
            if (p == nullptr)
            {
                throw std::bad_alloc();  // This is a test suite failure, not a failed test. Mind the difference.
            }
            // Random-fill the memory to make sure no assumptions are made about its contents.
            std::generate_n(reinterpret_cast<std::byte*>(p), amount, &TestAllocator::getRandomByte);
            allocated_.emplace(p, amount);
        }
        return p;
    }

    /// This overload is needed to avoid unnecessary const_cast<> in tests.
    /// The casts are needed because allocated memory is pointed to by const-qualified pointers.
    /// This is due to certain fundamental limitations of C; see the API docs for info.
    void deallocate(const void* const pointer)
    {
        deallocate(const_cast<void*>(pointer));  // NOLINT
    }

    void deallocate(void* const pointer)
    {
        if (pointer != nullptr)
        {
            const auto it = allocated_.find(pointer);
            if (it == std::end(allocated_))
            {
                throw std::logic_error("Heap corruption: an attempt to deallocate memory that is not allocated");
            }
            // Damage the memory to make sure it's not used after deallocation.
            std::generate_n(reinterpret_cast<std::byte*>(pointer), it->second, &TestAllocator::getRandomByte);
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            std::free(it->first);  // NOLINT
            allocated_.erase(it);
        }
    }

    [[nodiscard]] auto getNumAllocatedFragments() const { return std::size(allocated_); }

    [[nodiscard]] auto getTotalAllocatedAmount() const -> std::size_t
    {
        std::size_t out = 0U;
        for (auto [_, size] : allocated_)
        {
            out += size;
        }
        return out;
    }

    [[nodiscard]] auto getAllocationCeiling() const { return ceiling_; }
    void               setAllocationCeiling(const std::size_t amount) { ceiling_ = amount; }
};

/// An enhancing wrapper over the library to remove boilerplate from the tests.
class Instance
{
    CanardInstance canard_ = canardInit(&Instance::trampolineAllocate, &Instance::trampolineDeallocate);
    TestAllocator  allocator_;

    static auto trampolineAllocate(CanardInstance* const ins, const std::size_t amount) -> void*
    {
        auto p = reinterpret_cast<Instance*>(ins->user_reference);
        return p->allocator_.allocate(amount);
    }

    static void trampolineDeallocate(CanardInstance* const ins, void* const pointer)
    {
        auto p = reinterpret_cast<Instance*>(ins->user_reference);
        p->allocator_.deallocate(pointer);
    }

public:
    Instance() { canard_.user_reference = this; }

    virtual ~Instance() = default;

    Instance(const Instance&)  = delete;
    Instance(const Instance&&) = delete;
    auto operator=(const Instance&) -> Instance& = delete;
    auto operator=(const Instance &&) -> Instance& = delete;

    [[nodiscard]] auto txPush(const CanardTransfer& transfer) { return canardTxPush(&canard_, &transfer); }

    [[nodiscard]] auto txPeek(CanardFrame& out_frame) const { return canardTxPeek(&canard_, &out_frame); }

    void txPop() { canardTxPop(&canard_); }

    [[nodiscard]] auto rxAccept(const CanardFrame& frame,
                                const uint8_t      redundant_transport_index,
                                CanardTransfer&    out_transfer)
    {
        return canardRxAccept(&canard_, &frame, redundant_transport_index, &out_transfer);
    }

    [[nodiscard]] auto rxSubscribe(const CanardTransferKind transfer_kind,
                                   const CanardPortID       port_id,
                                   const std::size_t        payload_size_max,
                                   const CanardMicrosecond  transfer_id_timeout_usec,
                                   CanardRxSubscription&    out_subscription)
    {
        return canardRxSubscribe(&canard_,
                                 transfer_kind,
                                 port_id,
                                 payload_size_max,
                                 transfer_id_timeout_usec,
                                 &out_subscription);
    }

    [[nodiscard]] auto rxUnsubscribe(const CanardTransferKind transfer_kind, const CanardPortID port_id)
    {
        return canardRxUnsubscribe(&canard_, transfer_kind, port_id);
    }

    [[nodiscard]] auto getNodeID() const { return canard_.node_id; }
    void               setNodeID(const std::uint8_t x) { canard_.node_id = x; }

    [[nodiscard]] auto getMTU() const { return canard_.mtu_bytes; }
    void               setMTU(const std::size_t x) { canard_.mtu_bytes = x; }

    [[nodiscard]] auto getTxQueueRoot() const
    {
        return reinterpret_cast<const exposed::TxQueueItem*>(canard_._tx_queue);
    }

    [[nodiscard]] auto getTxQueueLength() const
    {
        std::size_t out = 0U;
        auto        p   = getTxQueueRoot();
        while (p != nullptr)
        {
            ++out;
            p = p->next;
        }
        return out;
    }

    [[nodiscard]] auto getAllocator() -> TestAllocator& { return allocator_; }

    [[nodiscard]] auto getInstance() -> CanardInstance& { return canard_; }
    [[nodiscard]] auto getInstance() const -> const CanardInstance& { return canard_; }
};

}  // namespace helpers
