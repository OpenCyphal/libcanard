// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016 UAVCAN Development Team.

#include "exposed.hpp"
#include "helpers.hpp"
#include "catch.hpp"
#include <cstring>

TEST_CASE("TxBasic0")
{
    using exposed::TxItem;

    helpers::Instance ins;
    helpers::TxQueue  que(200, CANARD_MTU_CAN_FD);

    auto& alloc = ins.getAllocator();

    std::array<std::uint8_t, 1024> payload{};
    for (std::size_t i = 0; i < std::size(payload); i++)
    {
        payload.at(i) = static_cast<std::uint8_t>(i & 0xFFU);
    }

    REQUIRE(CANARD_NODE_ID_UNSET == ins.getNodeID());
    REQUIRE(CANARD_MTU_CAN_FD == que.getMTU());
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    alloc.setAllocationCeiling(400);

    CanardTransferMetadata meta{};

    // Single-frame with padding.
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.port_id        = 321;
    meta.remote_node_id = CANARD_NODE_ID_UNSET;
    meta.transfer_id    = 21;
    REQUIRE(1 == que.push(&ins.getInstance(), 1'000'000'000'000ULL, meta, 8, payload.data()));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    REQUIRE(10 < alloc.getTotalAllocatedAmount());
    REQUIRE(160 > alloc.getTotalAllocatedAmount());
    REQUIRE(que.peekTxItem()->frame.timestamp_usec == 1'000'000'000'000ULL);
    REQUIRE(que.peekTxItem()->frame.payload_size == 12);  // Three bytes of padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(0) == 0);    // Payload start.
    REQUIRE(que.peekTxItem()->getPayloadByte(1) == 1);
    REQUIRE(que.peekTxItem()->getPayloadByte(2) == 2);
    REQUIRE(que.peekTxItem()->getPayloadByte(3) == 3);
    REQUIRE(que.peekTxItem()->getPayloadByte(4) == 4);
    REQUIRE(que.peekTxItem()->getPayloadByte(5) == 5);
    REQUIRE(que.peekTxItem()->getPayloadByte(6) == 6);
    REQUIRE(que.peekTxItem()->getPayloadByte(7) == 7);   // Payload end.
    REQUIRE(que.peekTxItem()->getPayloadByte(8) == 0);   // Padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(9) == 0);   // Padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(10) == 0);  // Padding.
    REQUIRE(que.peekTxItem()->isStartOfTransfer());      // Tail byte at the end.
    REQUIRE(que.peekTxItem()->isEndOfTransfer());
    REQUIRE(que.peekTxItem()->isToggleBitSet());

    // Multi-frame. Priority low, inserted at the end of the TX queue.
    meta.priority    = CanardPriorityLow;
    meta.transfer_id = 22;
    que.setMTU(CANARD_MTU_CAN_CLASSIC);
    ins.setNodeID(42);
    REQUIRE(2 == que.push(&ins.getInstance(), 1'000'000'000'100ULL, meta, 8, payload.data()));  // 8 bytes --> 2 frames
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());

    // Check the TX queue.
    {
        const auto q = que.linearize();
        REQUIRE(3 == q.size());
        REQUIRE(q.at(0)->frame.timestamp_usec == 1'000'000'000'000ULL);
        REQUIRE(q.at(0)->frame.payload_size == 12);
        REQUIRE(q.at(0)->isStartOfTransfer());
        REQUIRE(q.at(0)->isEndOfTransfer());
        REQUIRE(q.at(0)->isToggleBitSet());
        //
        REQUIRE(q.at(1)->frame.timestamp_usec == 1'000'000'000'100ULL);
        REQUIRE(q.at(1)->frame.payload_size == 8);
        REQUIRE(q.at(1)->isStartOfTransfer());
        REQUIRE(!q.at(1)->isEndOfTransfer());
        REQUIRE(q.at(1)->isToggleBitSet());
        //
        REQUIRE(q.at(2)->frame.timestamp_usec == 1'000'000'000'100ULL);
        REQUIRE(q.at(2)->frame.payload_size == 4);  // One leftover, two CRC, one tail.
        REQUIRE(!q.at(2)->isStartOfTransfer());
        REQUIRE(q.at(2)->isEndOfTransfer());
        REQUIRE(!q.at(2)->isToggleBitSet());
    }

    // Single-frame, OOM.
    alloc.setAllocationCeiling(alloc.getTotalAllocatedAmount());  // Seal up the heap at this level.
    meta.priority    = CanardPriorityLow;
    meta.transfer_id = 23;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY == que.push(&ins.getInstance(), 1'000'000'000'200ULL, meta, 1, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());

    // Multi-frame, first frame added successfully, then OOM. The entire transaction rejected.
    alloc.setAllocationCeiling(alloc.getTotalAllocatedAmount() + sizeof(TxItem) + 10U);
    meta.priority    = CanardPriorityHigh;
    meta.transfer_id = 24;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY ==
            que.push(&ins.getInstance(), 1'000'000'000'300ULL, meta, 100, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());

    // Pop the queue.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(8))).value)
    constexpr std::uint16_t CRC8  = 0x178DU;
    const CanardFrame*      frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 12);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 8));
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[8]);   // Padding.
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[9]);   // Padding.
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[10]);  // Padding.
    REQUIRE((0b11100000U | 21U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[11]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'000ULL);
    frame = que.peek();
    REQUIRE(nullptr != frame);  // Make sure we get the same frame again.
    REQUIRE(frame->payload_size == 12);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 8));
    REQUIRE((0b11100000U | 21U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[11]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 8);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 7));
    REQUIRE((0b10100000U | 22U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[7]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'100ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 4);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 7U, 1));
    REQUIRE((CRC8 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE((CRC8 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[2]);
    REQUIRE((0b01000000U | 22U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[3]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'100ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr == frame);
    REQUIRE(nullptr == que.pop(nullptr));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr == frame);

    alloc.setAllocationCeiling(1000);

    // Multi-frame, success. CRC split over the frame boundary.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(61))).value)
    constexpr std::uint16_t CRC61 = 0x554EU;
    que.setMTU(32);
    meta.priority    = CanardPriorityFast;
    meta.transfer_id = 25;
    // CRC takes 2 bytes at the end; 3 frames: (31+1) + (30+1+1) + (1+1)
    REQUIRE(3 == que.push(&ins.getInstance(), 1'000'000'001'000ULL, meta, 31 + 30, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(40 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 31));
    REQUIRE((0b10100000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 31U, 30));
    REQUIRE((CRC61 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[30]);
    REQUIRE((0b00000000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 2);  // The last byte of CRC plus the tail byte.
    REQUIRE((CRC61 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE((0b01100000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Multi-frame, success. CRC is in the last frame->
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(62))).value)
    constexpr std::uint16_t CRC62 = 0xA3AEU;
    que.setMTU(32);
    meta.priority    = CanardPrioritySlow;
    meta.transfer_id = 26;
    // CRC takes 2 bytes at the end; 3 frames: (31+1) + (31+1) + (2+1)
    REQUIRE(3 == que.push(&ins.getInstance(), 1'000'000'002'000ULL, meta, 31 + 31, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(40 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 31));
    REQUIRE((0b10100000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 31U, 31));
    REQUIRE((0b00000000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 3);  // The CRC plus the tail byte.
    REQUIRE((CRC62 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE((CRC62 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE((0b01100000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[2]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Multi-frame with padding.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(112)) + [0] * 12).value)
    constexpr std::uint16_t CRC112Padding12 = 0xE7A5U;
    que.setMTU(64);
    meta.priority    = CanardPriorityImmediate;
    meta.transfer_id = 27;
    // 63 + 63 - 2 = 124 bytes; 124 - 112 = 12 bytes of padding.
    REQUIRE(2 == que.push(&ins.getInstance(), 1'000'000'003'000ULL, meta, 112, payload.data()));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 64);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 63));
    REQUIRE((0b10100000U | 27U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[63]);
    REQUIRE(frame->timestamp_usec == 1'000'000'003'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 64);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 63U, 49));
    REQUIRE(std::all_of(reinterpret_cast<const std::uint8_t*>(frame->payload) + 49,  // Check padding.
                        reinterpret_cast<const std::uint8_t*>(frame->payload) + 61,
                        [](auto x) { return x == 0U; }));
    REQUIRE((CRC112Padding12 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[61]);    // CRC
    REQUIRE((CRC112Padding12 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[62]);  // CRC
    REQUIRE((0b01000000U | 27U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[63]);        // Tail
    REQUIRE(frame->timestamp_usec == 1'000'000'003'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Single-frame empty.
    meta.transfer_id = 28;
    REQUIRE(1 == que.push(&ins.getInstance(), 1'000'000'004'000ULL, meta, 0, nullptr));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    REQUIRE(120 > alloc.getTotalAllocatedAmount());
    REQUIRE(que.peekTxItem()->frame.timestamp_usec == 1'000'000'004'000ULL);
    REQUIRE(que.peekTxItem()->frame.payload_size == 1);
    REQUIRE(que.peekTxItem()->isStartOfTransfer());
    REQUIRE(que.peekTxItem()->isEndOfTransfer());
    REQUIRE(que.peekTxItem()->isToggleBitSet());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 1);
    REQUIRE((0b11100000U | 28U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE(frame->timestamp_usec == 1'000'000'004'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Nothing left to peek at.
    frame = que.peek();
    REQUIRE(nullptr == frame);

    // Invalid transfer.
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.remote_node_id = 42;
    meta.transfer_id    = 123;
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            que.push(&ins.getInstance(), 1'000'000'005'000ULL, meta, 8, payload.data()));
    frame = que.peek();
    REQUIRE(nullptr == frame);

    // Error handling.
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, nullptr, 0, nullptr, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, nullptr, 0, &meta, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, &ins.getInstance(), 0, &meta, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            canardTxPush(&que.getInstance(), &ins.getInstance(), 0, nullptr, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == que.push(&ins.getInstance(), 1'000'000'006'000ULL, meta, 1, nullptr));

    REQUIRE(nullptr == canardTxPeek(nullptr));
    REQUIRE(nullptr == canardTxPop(nullptr, nullptr));             // No effect.
    REQUIRE(nullptr == canardTxPop(&que.getInstance(), nullptr));  // No effect.
}

TEST_CASE("TxBasic1")
{
    helpers::Instance ins;
    helpers::TxQueue  que(3, CANARD_MTU_CAN_FD);  // Limit capacity at 3 frames.

    auto& alloc = ins.getAllocator();

    std::array<std::uint8_t, 1024> payload{};
    for (std::size_t i = 0; i < std::size(payload); i++)
    {
        payload.at(i) = static_cast<std::uint8_t>(i & 0xFFU);
    }

    REQUIRE(CANARD_NODE_ID_UNSET == ins.getNodeID());
    REQUIRE(CANARD_MTU_CAN_FD == que.getMTU());
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    CanardTransferMetadata meta{};

    // Single-frame with padding.
    meta.priority       = CanardPriorityNominal;
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.port_id        = 321;
    meta.remote_node_id = CANARD_NODE_ID_UNSET;
    meta.transfer_id    = 21;
    REQUIRE(1 == que.push(&ins.getInstance(), 1'000'000'000'000ULL, meta, 8, payload.data()));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    REQUIRE(10 < alloc.getTotalAllocatedAmount());
    REQUIRE(160 > alloc.getTotalAllocatedAmount());
    REQUIRE(que.peekTxItem()->frame.timestamp_usec == 1'000'000'000'000ULL);
    REQUIRE(que.peekTxItem()->frame.payload_size == 12);  // Three bytes of padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(0) == 0);    // Payload start.
    REQUIRE(que.peekTxItem()->getPayloadByte(1) == 1);
    REQUIRE(que.peekTxItem()->getPayloadByte(2) == 2);
    REQUIRE(que.peekTxItem()->getPayloadByte(3) == 3);
    REQUIRE(que.peekTxItem()->getPayloadByte(4) == 4);
    REQUIRE(que.peekTxItem()->getPayloadByte(5) == 5);
    REQUIRE(que.peekTxItem()->getPayloadByte(6) == 6);
    REQUIRE(que.peekTxItem()->getPayloadByte(7) == 7);   // Payload end.
    REQUIRE(que.peekTxItem()->getPayloadByte(8) == 0);   // Padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(9) == 0);   // Padding.
    REQUIRE(que.peekTxItem()->getPayloadByte(10) == 0);  // Padding.
    REQUIRE(que.peekTxItem()->isStartOfTransfer());      // Tail byte at the end.
    REQUIRE(que.peekTxItem()->isEndOfTransfer());
    REQUIRE(que.peekTxItem()->isToggleBitSet());

    // Multi-frame. Priority low, inserted at the end of the TX queue. Two frames exhaust the capacity of the queue.
    meta.priority    = CanardPriorityLow;
    meta.transfer_id = 22;
    que.setMTU(CANARD_MTU_CAN_CLASSIC);
    ins.setNodeID(42);
    REQUIRE(2 == que.push(&ins.getInstance(), 1'000'000'000'100ULL, meta, 8, payload.data()));  // 8 bytes --> 2 frames
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());

    // Check the TX queue.
    {
        const auto q = que.linearize();
        REQUIRE(3 == q.size());
        REQUIRE(q.at(0)->frame.timestamp_usec == 1'000'000'000'000ULL);
        REQUIRE(q.at(0)->frame.payload_size == 12);
        REQUIRE(q.at(0)->isStartOfTransfer());
        REQUIRE(q.at(0)->isEndOfTransfer());
        REQUIRE(q.at(0)->isToggleBitSet());
        //
        REQUIRE(q.at(1)->frame.timestamp_usec == 1'000'000'000'100ULL);
        REQUIRE(q.at(1)->frame.payload_size == 8);
        REQUIRE(q.at(1)->isStartOfTransfer());
        REQUIRE(!q.at(1)->isEndOfTransfer());
        REQUIRE(q.at(1)->isToggleBitSet());
        //
        REQUIRE(q.at(2)->frame.timestamp_usec == 1'000'000'000'100ULL);
        REQUIRE(q.at(2)->frame.payload_size == 4);  // One leftover, two CRC, one tail.
        REQUIRE(!q.at(2)->isStartOfTransfer());
        REQUIRE(q.at(2)->isEndOfTransfer());
        REQUIRE(!q.at(2)->isToggleBitSet());
    }

    // Single-frame, OOM reported but the heap is not exhausted (because queue is filled up).
    meta.priority    = CanardPriorityLow;
    meta.transfer_id = 23;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY == que.push(&ins.getInstance(), 1'000'000'000'200ULL, meta, 1, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());

    // Multi-frame, no frames are added -- bail early always.
    meta.priority    = CanardPriorityHigh;
    meta.transfer_id = 24;
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY ==
            que.push(&ins.getInstance(), 1'000'000'000'300ULL, meta, 100, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(20 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());

    // Pop the queue.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(8))).value)
    constexpr std::uint16_t CRC8  = 0x178DU;
    const CanardFrame*      frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 12);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 8));
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[8]);   // Padding.
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[9]);   // Padding.
    REQUIRE(0 == reinterpret_cast<const std::uint8_t*>(frame->payload)[10]);  // Padding.
    REQUIRE((0b11100000U | 21U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[11]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'000ULL);
    frame = que.peek();
    REQUIRE(nullptr != frame);  // Make sure we get the same frame again.
    REQUIRE(frame->payload_size == 12);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 8));
    REQUIRE((0b11100000U | 21U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[11]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 8);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 7));
    REQUIRE((0b10100000U | 22U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[7]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'100ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 4);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 7U, 1));
    REQUIRE((CRC8 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE((CRC8 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[2]);
    REQUIRE((0b01000000U | 22U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[3]);
    REQUIRE(frame->timestamp_usec == 1'000'000'000'100ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr == frame);
    REQUIRE(nullptr == que.pop(frame));  // Invocation when empty has no effect.
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr == frame);

    // Multi-frame, success. CRC split over the frame boundary.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(61))).value)
    constexpr std::uint16_t CRC61 = 0x554EU;
    que.setMTU(32);
    meta.priority    = CanardPriorityFast;
    meta.transfer_id = 25;
    // CRC takes 2 bytes at the end; 3 frames: (31+1) + (30+1+1) + (1+1)
    REQUIRE(3 == que.push(&ins.getInstance(), 1'000'000'001'000ULL, meta, 31 + 30, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(40 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 31));
    REQUIRE((0b10100000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 31U, 30));
    REQUIRE((CRC61 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[30]);
    REQUIRE((0b00000000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 2);  // The last byte of CRC plus the tail byte.
    REQUIRE((CRC61 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE((0b01100000U | 25U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE(frame->timestamp_usec == 1'000'000'001'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Multi-frame, success. CRC is in the last frame->
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(62))).value)
    constexpr std::uint16_t CRC62 = 0xA3AEU;
    que.setMTU(32);
    meta.priority    = CanardPrioritySlow;
    meta.transfer_id = 26;
    // CRC takes 2 bytes at the end; 3 frames: (31+1) + (31+1) + (2+1)
    REQUIRE(3 == que.push(&ins.getInstance(), 1'000'000'002'000ULL, meta, 31 + 31, payload.data()));
    REQUIRE(3 == que.getSize());
    REQUIRE(3 == alloc.getNumAllocatedFragments());
    REQUIRE(40 < alloc.getTotalAllocatedAmount());
    REQUIRE(400 > alloc.getTotalAllocatedAmount());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 31));
    REQUIRE((0b10100000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 32);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 31U, 31));
    REQUIRE((0b00000000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[31]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 3);  // The CRC plus the tail byte.
    REQUIRE((CRC62 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE((CRC62 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[1]);
    REQUIRE((0b01100000U | 26U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[2]);
    REQUIRE(frame->timestamp_usec == 1'000'000'002'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Multi-frame with padding.
    // hex(pyuavcan.transport.commons.crc.CRC16CCITT.new(list(range(112)) + [0] * 12).value)
    constexpr std::uint16_t CRC112Padding12 = 0xE7A5U;
    que.setMTU(64);
    meta.priority    = CanardPriorityImmediate;
    meta.transfer_id = 27;
    // 63 + 63 - 2 = 124 bytes; 124 - 112 = 12 bytes of padding.
    REQUIRE(2 == que.push(&ins.getInstance(), 1'000'000'003'000ULL, meta, 112, payload.data()));
    REQUIRE(2 == que.getSize());
    REQUIRE(2 == alloc.getNumAllocatedFragments());
    // Read the generated frames.
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 64);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data(), 63));
    REQUIRE((0b10100000U | 27U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[63]);
    REQUIRE(frame->timestamp_usec == 1'000'000'003'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 64);
    REQUIRE(0 == std::memcmp(frame->payload, payload.data() + 63U, 49));
    REQUIRE(std::all_of(reinterpret_cast<const std::uint8_t*>(frame->payload) + 49,  // Check padding.
                        reinterpret_cast<const std::uint8_t*>(frame->payload) + 61,
                        [](auto x) { return x == 0U; }));
    REQUIRE((CRC112Padding12 >> 8U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[61]);    // CRC
    REQUIRE((CRC112Padding12 & 0xFFU) == reinterpret_cast<const std::uint8_t*>(frame->payload)[62]);  // CRC
    REQUIRE((0b01000000U | 27U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[63]);        // Tail
    REQUIRE(frame->timestamp_usec == 1'000'000'003'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Single-frame empty.
    meta.transfer_id = 28;
    REQUIRE(1 == que.push(&ins.getInstance(), 1'000'000'004'000ULL, meta, 0, nullptr));
    REQUIRE(1 == que.getSize());
    REQUIRE(1 == alloc.getNumAllocatedFragments());
    REQUIRE(120 > alloc.getTotalAllocatedAmount());
    REQUIRE(que.peekTxItem()->frame.timestamp_usec == 1'000'000'004'000ULL);
    REQUIRE(que.peekTxItem()->frame.payload_size == 1);
    REQUIRE(que.peekTxItem()->isStartOfTransfer());
    REQUIRE(que.peekTxItem()->isEndOfTransfer());
    REQUIRE(que.peekTxItem()->isToggleBitSet());
    frame = que.peek();
    REQUIRE(nullptr != frame);
    REQUIRE(frame->payload_size == 1);
    REQUIRE((0b11100000U | 28U) == reinterpret_cast<const std::uint8_t*>(frame->payload)[0]);
    REQUIRE(frame->timestamp_usec == 1'000'000'004'000ULL);
    ins.getAllocator().deallocate(que.pop(frame));
    REQUIRE(0 == que.getSize());
    REQUIRE(0 == alloc.getNumAllocatedFragments());

    // Nothing left to peek at.
    frame = que.peek();
    REQUIRE(nullptr == frame);

    // Invalid transfer.
    meta.transfer_kind  = CanardTransferKindMessage;
    meta.remote_node_id = 42;
    meta.transfer_id    = 123;
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            que.push(&ins.getInstance(), 1'000'000'005'000ULL, meta, 8, payload.data()));
    frame = que.peek();
    REQUIRE(nullptr == frame);

    // Error handling.
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, nullptr, 0, nullptr, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, nullptr, 0, &meta, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == canardTxPush(nullptr, &ins.getInstance(), 0, &meta, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==
            canardTxPush(&que.getInstance(), &ins.getInstance(), 0, nullptr, 0, nullptr));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT == que.push(&ins.getInstance(), 1'000'000'006'000ULL, meta, 1, nullptr));

    REQUIRE(nullptr == canardTxPeek(nullptr));
    REQUIRE(nullptr == canardTxPop(nullptr, nullptr));             // No effect.
    REQUIRE(nullptr == canardTxPop(&que.getInstance(), nullptr));  // No effect.
}
