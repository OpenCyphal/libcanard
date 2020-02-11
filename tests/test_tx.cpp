// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"
#include "helpers.hpp"

TEST_CASE("TxBasic")
{
    using internals::TxQueueItem;

    helpers::Instance ins;

    auto& alloc = ins.getAllocator();

    std::array<std::uint8_t, 1024> payload{};
    for (std::size_t i = 0; i < std::size(payload); i++)
    {
        payload.at(i) = static_cast<std::uint8_t>(i & 0xFFU);
    }

    REQUIRE(CANARD_NODE_ID_UNSET == ins.getNodeID());
    REQUIRE(CANARD_MTU_CAN_FD == ins.getMTU());
    REQUIRE(nullptr == ins.getTxQueueRoot());
    REQUIRE(0 == ins.getTxQueueLength());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    alloc.setAllocationCeiling(200);

    CanardTransfer transfer{};
    transfer.payload = payload.data();

    // Single-frame, success.
    transfer.timestamp_usec = 1'000'000'000'000ULL;
    transfer.priority       = CanardPriorityNominal;
    transfer.transfer_kind  = CanardTransferKindMessage;
    transfer.port_id        = 321;
    transfer.remote_node_id = CANARD_NODE_ID_UNSET;
    transfer.transfer_id    = 21;
    transfer.payload_size   = 8;
    REQUIRE(1 == ins.txPush(transfer));
    REQUIRE(1 == ins.getTxQueueLength());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    REQUIRE(10 < alloc.getTotalAllocatedAmount());
    REQUIRE(80 > alloc.getTotalAllocatedAmount());
    REQUIRE(ins.getTxQueueRoot()->deadline_usec == 1'000'000'000'000ULL);
    REQUIRE(ins.getTxQueueRoot()->payload_size == 9);
    REQUIRE(ins.getTxQueueRoot()->isStartOfTransfer());
    REQUIRE(ins.getTxQueueRoot()->isEndOfTransfer());
    REQUIRE(ins.getTxQueueRoot()->isToggleBitSet());

    // Multi-frame, success. Priority low, inserted at the end of the TX queue.
    transfer.timestamp_usec = 1'000'000'000'100ULL;
    transfer.priority       = CanardPriorityLow;
    transfer.transfer_id    = 22;
    transfer.payload_size   = 8;
    ins.setMTU(CANARD_MTU_CAN_CLASSIC);
    ins.setNodeID(42);
    REQUIRE(2 == ins.txPush(transfer));  // 8 bytes --> 2 frames
    REQUIRE(3 == ins.getTxQueueLength());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(200 > alloc.getTotalAllocatedAmount());

    // Check the TX queue.
    {
        auto q = ins.getTxQueueRoot();
        REQUIRE(q != nullptr);
        REQUIRE(q->deadline_usec == 1'000'000'000'000ULL);
        REQUIRE(q->payload_size == 9);
        REQUIRE(q->isStartOfTransfer());
        REQUIRE(q->isEndOfTransfer());
        REQUIRE(q->isToggleBitSet());
        q = q->next;
        REQUIRE(q != nullptr);
        REQUIRE(q->deadline_usec == 1'000'000'000'100ULL);
        REQUIRE(q->payload_size == 8);
        REQUIRE(q->isStartOfTransfer());
        REQUIRE(!q->isEndOfTransfer());
        REQUIRE(q->isToggleBitSet());
        q = q->next;
        REQUIRE(q != nullptr);
        REQUIRE(q->deadline_usec == 1'000'000'000'100ULL);
        REQUIRE(q->payload_size == 4);  // One leftover, two CRC, one tail.
        REQUIRE(!q->isStartOfTransfer());
        REQUIRE(q->isEndOfTransfer());
        REQUIRE(!q->isToggleBitSet());
        q = q->next;
        REQUIRE(q == nullptr);
    }

    // Single-frame, OOM.
    alloc.setAllocationCeiling(alloc.getTotalAllocatedAmount());  // Seal up the heap at this level.
    transfer.timestamp_usec = 1'000'000'000'200ULL;
    transfer.priority       = CanardPriorityLow;
    transfer.transfer_id    = 23;
    transfer.payload_size   = 1;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY == ins.txPush(transfer));
    REQUIRE(3 == ins.getTxQueueLength());
    REQUIRE(3 == alloc.getNumAllocatedFragments());

    // Multi-frame, first frame added successfully, then OOM. The entire transaction rejected.
    alloc.setAllocationCeiling(alloc.getTotalAllocatedAmount() + sizeof(TxQueueItem) + 10U);
    transfer.timestamp_usec = 1'000'000'000'300ULL;
    transfer.priority       = CanardPriorityHigh;
    transfer.transfer_id    = 24;
    transfer.payload_size   = 100;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY == ins.txPush(transfer));
    REQUIRE(3 == ins.getTxQueueLength());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(200 > alloc.getTotalAllocatedAmount());
}
