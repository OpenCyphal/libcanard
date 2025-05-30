// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016 Cyphal Development Team.

#pragma once

#include "canard.h"
#include "exposed.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if !(defined(CANARD_VERSION_MAJOR) && defined(CANARD_VERSION_MINOR))
#    error "Library version not defined"
#endif

#if !(defined(CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR) && defined(CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR))
#    error "Cyphal specification version not defined"
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
auto getRandomNatural(const std::size_t upper_open) -> T
{
    return static_cast<T>(static_cast<std::size_t>(std::rand()) % upper_open);  // NOLINT
}

template <typename F>
static inline void traverse(const CanardTreeNode* const root, const F& fun)  // NOLINT recursion
{
    if (root != nullptr)
    {
        traverse<F>(root->lr[0], fun);
        fun(root);
        traverse<F>(root->lr[1], fun);
    }
}

/// An allocator that sits on top of the standard malloc() providing additional testing capabilities.
/// It allows the user to specify the maximum amount of memory that can be allocated; further requests will emulate OOM.
class TestAllocator final
{
public:
    TestAllocator()                                         = default;
    TestAllocator(const TestAllocator&)                     = delete;
    TestAllocator(const TestAllocator&&)                    = delete;
    auto operator=(const TestAllocator&) -> TestAllocator&  = delete;
    auto operator=(const TestAllocator&&) -> TestAllocator& = delete;

    ~TestAllocator()
    {
        const std::unique_lock locker(lock_);
        for (const auto& pair : allocated_)
        {
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            std::free(pair.first - canary_.size());  // NOLINT
        }
    }

    [[nodiscard]] auto allocate(const std::size_t amount) -> void*
    {
        const std::unique_lock locker(lock_);
        std::uint8_t*          p = nullptr;
        if ((amount > 0U) && ((getTotalAllocatedAmount() + amount) <= ceiling_))
        {
            const auto amount_with_canaries = amount + (canary_.size() * 2U);
            // Clang-tidy complains about manual memory management. Suppressed because we need it for testing purposes.
            p = static_cast<std::uint8_t*>(std::malloc(amount_with_canaries));  // NOLINT
            if (p == nullptr)
            {
                throw std::bad_alloc();  // This is a test suite failure, not a failed test. Mind the difference.
            }
            p += canary_.size();
            std::generate_n(p, amount, []() { return getRandomNatural<std::uint8_t>(256U); });
            std::memcpy(p - canary_.size(), canary_.begin(), canary_.size());
            std::memcpy(p + amount, canary_.begin(), canary_.size());
            allocated_.emplace(p, amount);
        }
        return p;
    }

    void deallocate(void* const user_pointer, const std::size_t amount)
    {
        if (user_pointer != nullptr)
        {
            const std::unique_lock locker(lock_);
            const auto             it = allocated_.find(static_cast<std::uint8_t*>(user_pointer));
            if (it == std::end(allocated_))  // Catch an attempt to deallocate memory that is not allocated.
            {
                throw std::logic_error("Attempted to deallocate memory that was never allocated; ptr=" +
                                       std::to_string(reinterpret_cast<std::uint64_t>(user_pointer)));
            }
            const auto [p, expected_amount] = *it;
            if (amount != expected_amount)
            {
                throw std::logic_error("Attempted to deallocate wrong size memory at ptr=" +
                                       std::to_string(reinterpret_cast<std::uint64_t>(user_pointer)));
            }
            if ((0 != std::memcmp(p - canary_.size(), canary_.begin(), canary_.size())) ||
                (0 != std::memcmp(p + amount, canary_.begin(), canary_.size())))
            {
                throw std::logic_error("Dead canary detected at ptr=" +
                                       std::to_string(reinterpret_cast<std::uint64_t>(user_pointer)));
            }
            std::generate_n(p - canary_.size(),  // Damage the memory to make sure it's not used after deallocation.
                            amount + (canary_.size() * 2U),
                            []() { return getRandomNatural<std::uint8_t>(256U); });
            std::free(p - canary_.size());  // NOLINT we require manual memory management here.
            allocated_.erase(it);
        }
    }

    [[nodiscard]] auto getNumAllocatedFragments() const
    {
        const std::unique_lock locker(lock_);
        return std::size(allocated_);
    }

    [[nodiscard]] auto getTotalAllocatedAmount() const -> std::size_t
    {
        const std::unique_lock locker(lock_);
        std::size_t            out = 0U;
        for (const auto& pair : allocated_)
        {
            out += pair.second;
        }
        return out;
    }

    [[nodiscard]] auto getAllocationCeiling() const { return static_cast<std::size_t>(ceiling_); }
    void               setAllocationCeiling(const std::size_t amount) { ceiling_ = amount; }

private:
    static auto makeCanary() -> std::array<std::uint8_t, 256>
    {
        std::array<std::uint8_t, 256> out{};
        std::generate_n(out.begin(), out.size(), []() { return getRandomNatural<std::uint8_t>(256U); });
        return out;
    }

    const std::array<std::uint8_t, 256> canary_ = makeCanary();

    mutable std::recursive_mutex                   lock_;
    std::unordered_map<std::uint8_t*, std::size_t> allocated_;
    std::atomic<std::size_t>                       ceiling_ = std::numeric_limits<std::size_t>::max();
};

/// An enhancing wrapper over the library to remove boilerplate from the tests.
class Instance final
{
public:
    Instance()
    {
        canard_.user_reference        = this;
        canard_.memory.user_reference = this;
    }

    ~Instance() = default;

    Instance(const Instance&)                     = delete;
    Instance(const Instance&&)                    = delete;
    auto operator=(const Instance&) -> Instance&  = delete;
    auto operator=(const Instance&&) -> Instance& = delete;

    [[nodiscard]] auto makeCanardMemoryResource() -> CanardMemoryResource
    {
        return {this, trampolineDeallocate, trampolineAllocate};
    }

    [[nodiscard]] auto rxAccept(const CanardMicrosecond      timestamp_usec,
                                const CanardFrame&           frame,
                                const std::uint8_t           redundant_iface_index,
                                CanardRxTransfer&            out_transfer,
                                CanardRxSubscription** const out_subscription)
    {
        return canardRxAccept(&canard_, timestamp_usec, &frame, redundant_iface_index, &out_transfer, out_subscription);
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

    [[nodiscard]] auto rxHasSubscription(const CanardTransferKind transfer_kind, const CanardPortID port_id)
    {
        return canardRxGetSubscription(&canard_, transfer_kind, port_id, nullptr);
    }

    [[nodiscard]] auto rxGetSubscription(const CanardTransferKind transfer_kind, const CanardPortID port_id)
    {
        CanardRxSubscription* out_subscription = nullptr;
        canardRxGetSubscription(&canard_, transfer_kind, port_id, &out_subscription);
        return out_subscription;
    }

    /// The items are sorted by port-ID.
    [[nodiscard]] auto getSubs(const CanardTransferKind tk) const -> std::vector<const CanardRxSubscription*>
    {
        std::vector<const CanardRxSubscription*> out;
        traverse(canard_.rx_subscriptions[tk], [&](const CanardTreeNode* const item) {
            out.push_back(reinterpret_cast<const CanardRxSubscription*>(item));
        });
        return out;
    }
    [[nodiscard]] auto getMessageSubs() const { return getSubs(CanardTransferKindMessage); }
    [[nodiscard]] auto getResponseSubs() const { return getSubs(CanardTransferKindResponse); }
    [[nodiscard]] auto getRequestSubs() const { return getSubs(CanardTransferKindRequest); }

    [[nodiscard]] auto getNodeID() const { return canard_.node_id; }
    void               setNodeID(const std::uint8_t x) { canard_.node_id = x; }

    [[nodiscard]] auto getAllocator() -> TestAllocator& { return allocator_; }

    [[nodiscard]] auto getInstance() -> CanardInstance& { return canard_; }
    [[nodiscard]] auto getInstance() const -> const CanardInstance& { return canard_; }

private:
    static auto trampolineAllocate(void* const user_reference, const std::size_t size) -> void*
    {
        auto* p = static_cast<Instance*>(user_reference);
        return p->allocator_.allocate(size);
    }

    static void trampolineDeallocate(void* const user_reference, const std::size_t size, void* const pointer)
    {
        auto* p = static_cast<Instance*>(user_reference);
        p->allocator_.deallocate(pointer, size);
    }

    CanardInstance canard_ = canardInit({nullptr, &Instance::trampolineDeallocate, &Instance::trampolineAllocate});
    TestAllocator  allocator_;
};

class TxQueue final
{
public:
    TxQueue(const std::size_t capacity, const std::size_t mtu_bytes) :
        que_(canardTxInit(capacity, mtu_bytes, makeCanardMemoryResource()))
    {
        enforce(que_.user_reference == nullptr, "Incorrect initialization of the user reference in TxQueue");
        enforce(que_.mtu_bytes == mtu_bytes, "Incorrect MTU");
        que_.user_reference = this;  // This is simply to ensure it is not overwritten unexpectedly.
        checkInvariants();
    }
    TxQueue(const std::size_t capacity, const std::size_t mtu_bytes, const CanardMemoryResource memory) :
        que_(canardTxInit(capacity, mtu_bytes, memory))
    {
        enforce(que_.user_reference == nullptr, "Incorrect initialization of the user reference in TxQueue");
        enforce(que_.mtu_bytes == mtu_bytes, "Incorrect MTU");
        que_.user_reference = this;  // This is simply to ensure it is not overwritten unexpectedly.
        checkInvariants();
    }
    ~TxQueue() = default;

    TxQueue(const TxQueue&)                    = delete;
    TxQueue(TxQueue&&)                         = delete;
    auto operator=(const TxQueue&) -> TxQueue& = delete;
    auto operator=(TxQueue&&) -> TxQueue&      = delete;

    [[nodiscard]] auto getMTU() const { return que_.mtu_bytes; }
    void               setMTU(const std::size_t x) { que_.mtu_bytes = x; }

    [[nodiscard]] auto push(CanardInstance* const         ins,
                            const CanardMicrosecond       transmission_deadline_usec,
                            const CanardTransferMetadata& metadata,
                            const struct CanardPayload    payload,
                            const CanardMicrosecond       now_usec,
                            uint64_t&                     frames_expired)
    {
        checkInvariants();

        const auto size_before = que_.size;
        uint64_t   dropped     = 0;

        const auto ret = canardTxPush(&que_, ins, transmission_deadline_usec, &metadata, payload, now_usec, &dropped);
        const auto num_added = static_cast<std::size_t>(ret);

        enforce((ret < 0) || ((size_before + num_added - dropped) == que_.size), "Unexpected size change after push");
        checkInvariants();

        frames_expired = dropped;
        return ret;
    }
    [[nodiscard]] auto push(CanardInstance* const         ins,
                            const CanardMicrosecond       transmission_deadline_usec,
                            const CanardTransferMetadata& metadata,
                            const struct CanardPayload    payload,
                            const CanardMicrosecond       now_usec = 0)
    {
        uint64_t frames_expired = 0;
        return push(ins, transmission_deadline_usec, metadata, payload, now_usec, frames_expired);
    }

    [[nodiscard]] auto peek() const -> exposed::TxItem*
    {
        checkInvariants();
        const auto  before = que_.size;
        auto* const ret    = canardTxPeek(&que_);
        enforce(((ret == nullptr) ? (before == 0) : (before > 0)) && (que_.size == before), "Bad peek");
        checkInvariants();
        return static_cast<exposed::TxItem*>(ret);  // NOLINT static downcast
    }

    [[nodiscard]] auto pop(CanardTxQueueItem* const which) -> exposed::TxItem*
    {
        checkInvariants();
        const auto size_before  = que_.size;
        const auto* volatile pk = peek();
        auto* out               = canardTxPop(&que_, which);
        enforce(pk == out, "Peek/pop pointer mismatch");
        if (out == nullptr)
        {
            enforce((size_before == 0) && (que_.size == 0), "Bad empty pop");
        }
        else
        {
            enforce((size_before > 0) && (que_.size == (size_before - 1U)), "Bad non-empty pop");
        }
        checkInvariants();
        return static_cast<exposed::TxItem*>(out);  // NOLINT static downcast
    }

    void freeItem(Instance& ins, CanardTxQueueItem* const item) { canardTxFree(&que_, &ins.getInstance(), item); }

    using FrameHandler = std::function<std::int8_t(const CanardMicrosecond, CanardMutableFrame&)>;

    struct PollStats
    {
        std::uint64_t frames_expired = 0;
        std::uint64_t frames_failed  = 0;
    };

    [[nodiscard]] auto poll(Instance&               ins,
                            const CanardMicrosecond now_usec,
                            FrameHandler            frame_handler,
                            PollStats&              poll_stats)
    {
        if (!frame_handler)
        {
            return canardTxPoll(&que_,
                                &ins.getInstance(),
                                now_usec,
                                nullptr,
                                nullptr,
                                &poll_stats.frames_expired,
                                &poll_stats.frames_failed);
        }
        return canardTxPoll(
            &que_,
            &ins.getInstance(),
            now_usec,
            &frame_handler,
            [](auto* user_reference, const auto deadline_usec, auto* const frame) -> std::int8_t {
                const auto* const handler_ptr = static_cast<FrameHandler*>(user_reference);
                return (*handler_ptr)(deadline_usec, *frame);
            },
            &poll_stats.frames_expired,
            &poll_stats.frames_failed);
    }
    [[nodiscard]] auto poll(Instance& ins, const CanardMicrosecond now_usec, const FrameHandler& frame_handler)
    {
        PollStats ps;
        return poll(ins, now_usec, frame_handler, ps);
    }

    [[nodiscard]] auto getSize() const
    {
        std::size_t out = 0;
        traverse(que_.priority_root, [&](auto*) { out++; });
        enforce(que_.size == out, "Size miscalculation");
        return out;
    }

    [[nodiscard]] auto getDeadlineQueueSize() const
    {
        std::size_t out = 0;
        traverse(que_.deadline_root, [&](auto*) { out++; });
        enforce(que_.size == out, "Size miscalculation");
        return out;
    }

    [[nodiscard]] auto linearize() const -> std::vector<const exposed::TxItem*>
    {
        std::vector<const exposed::TxItem*> out;
        traverse(que_.priority_root, [&](const CanardTreeNode* const item) {
            out.push_back(reinterpret_cast<const exposed::TxItem*>(item));
        });
        enforce(out.size() == getSize(), "Internal error");
        return out;
    }

    [[nodiscard]] auto getInstance() -> CanardTxQueue& { return que_; }
    [[nodiscard]] auto getInstance() const -> const CanardTxQueue& { return que_; }

    [[nodiscard]] auto getAllocator() -> TestAllocator& { return allocator_; }
    [[nodiscard]] auto makeCanardMemoryResource() -> CanardMemoryResource
    {
        return {this, trampolineDeallocate, trampolineAllocate};
    }

private:
    static auto trampolineAllocate(void* const user_reference, const std::size_t size) -> void*
    {
        auto* p = static_cast<TxQueue*>(user_reference);
        return p->allocator_.allocate(size);
    }

    static void trampolineDeallocate(void* const user_reference, const std::size_t size, void* const pointer)
    {
        auto* p = static_cast<TxQueue*>(user_reference);
        p->allocator_.deallocate(pointer, size);
    }

    static void enforce(const bool expect_true, const std::string& message)
    {
        if (!expect_true)
        {
            throw std::logic_error("TxQueue invariant violation: " + message);
        }
    }

    void checkInvariants() const
    {
        enforce(que_.user_reference == this, "User reference damaged");
        enforce(que_.size == getSize(), "Size miscalculation");
        enforce(que_.size == getDeadlineQueueSize(), "Deadline queue size miscalculation");
    }

    TestAllocator allocator_;
    CanardTxQueue que_;
};

}  // namespace helpers
