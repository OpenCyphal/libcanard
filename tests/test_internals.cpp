// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"
#include "helpers.hpp"

TEST_CASE("TransferCRC")
{
    using internals::crcAdd;
    std::uint16_t crc = 0xFFFFU;

    crc = crcAdd(crc, 1, "1");
    crc = crcAdd(crc, 1, "2");
    crc = crcAdd(crc, 1, "3");
    REQUIRE(0x5BCEU == crc);  // Using Libuavcan as reference
    crc = crcAdd(crc, 6, "456789");
    REQUIRE(0x29B1U == crc);
}

TEST_CASE("SessionSpecifier")
{
    REQUIRE(0b000'00'0011001100110011'0'1010101 ==
            internals::makeMessageSessionSpecifier(0b0011001100110011, 0b1010101));
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==
            internals::makeServiceSessionSpecifier(0b0100110011, true, 0b1010101, 0b0101010));
    REQUIRE(0b000'10'0100110011'1010101'0101010 ==
            internals::makeServiceSessionSpecifier(0b0100110011, false, 0b0101010, 0b1010101));
}

TEST_CASE("getPresentationLayerMTU")
{
    auto ins = canardInit(&helpers::dummy_allocator::allocate, &helpers::dummy_allocator::free);
    REQUIRE(63 == internals::getPresentationLayerMTU(&ins));  // This is the default.
    ins.mtu_bytes = 0;
    REQUIRE(7 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 255;
    REQUIRE(63 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 32;
    REQUIRE(31 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 30;  // Round up.
    REQUIRE(31 == internals::getPresentationLayerMTU(&ins));
}

TEST_CASE("makeCANID")
{
    using internals::makeCANID;

    CanardTransfer            transfer{};
    std::vector<std::uint8_t> transfer_payload;

    const auto mk_transfer = [&](const CanardPriority             priority,
                                 const CanardTransferKind         kind,
                                 const std::uint16_t              port_id,
                                 const std::uint8_t               remote_node_id,
                                 const std::vector<std::uint8_t>& payload = {}) {
        transfer_payload        = payload;
        transfer.priority       = priority;
        transfer.transfer_kind  = kind;
        transfer.port_id        = port_id;
        transfer.remote_node_id = remote_node_id;
        transfer.payload        = transfer_payload.data();
        transfer.payload_size   = transfer_payload.size();
        return &transfer;
    };

    const auto crc123 = internals::crcAdd(0xFFFFU, 3, "\x01\x02\x03");

    union PriorityAlias
    {
        std::uint8_t   bits;
        CanardPriority prio;
    };

    // MESSAGE TRANSFERS
    REQUIRE(0b000'00'0011001100110011'0'1010101 ==  // Regular message.
            makeCANID(mk_transfer(CanardPriorityExceptional,
                                  CanardTransferKindMessage,
                                  0b0011001100110011,
                                  CANARD_NODE_ID_UNSET),
                      0b1010101,
                      7U));
    REQUIRE(0b111'00'0011001100110011'0'1010101 ==  // Regular message.
            makeCANID(mk_transfer(CanardPriorityOptional,
                                  CanardTransferKindMessage,
                                  0b0011001100110011,
                                  CANARD_NODE_ID_UNSET),
                      0b1010101,
                      7U));
    REQUIRE((0b010'01'0011001100110011'0'0000000U | (crc123 & CANARD_NODE_ID_MAX)) ==  // Anonymous message.
            makeCANID(mk_transfer(CanardPriorityFast,
                                  CanardTransferKindMessage,
                                  0b0011001100110011,
                                  CANARD_NODE_ID_UNSET,
                                  {1, 2, 3}),
                      128U,  // Invalid local node-ID.
                      7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Multi-frame anonymous messages are not allowed.
            makeCANID(mk_transfer(CanardPriorityImmediate,
                                  CanardTransferKindMessage,
                                  0b0011001100110011,
                                  CANARD_NODE_ID_UNSET,
                                  {1, 2, 3, 4, 5, 6, 7, 8}),
                      128U,  // Invalid local node-ID is treated as anonymous/unset.
                      7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad remote node-ID -- unicast messages not supported.
            makeCANID(mk_transfer(CanardPriorityHigh, CanardTransferKindMessage, 0b0011001100110011, 123U), 0U, 7U));
    REQUIRE(
        -CANARD_ERROR_INVALID_ARGUMENT ==  // Bad subject-ID.
        makeCANID(mk_transfer(CanardPriorityLow, CanardTransferKindMessage, 0xFFFFU, CANARD_NODE_ID_UNSET), 0U, 7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad priority.
            makeCANID(mk_transfer(PriorityAlias{123}.prio,
                                  CanardTransferKindMessage,
                                  0b0011001100110011,
                                  CANARD_NODE_ID_UNSET),
                      0b1010101,
                      7U));

    // SERVICE TRANSFERS
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==  // Request.
            makeCANID(mk_transfer(CanardPriorityExceptional, CanardTransferKindRequest, 0b0100110011, 0b0101010),
                      0b1010101,
                      7U));
    REQUIRE(0b111'10'0100110011'0101010'1010101 ==  // Response.
            makeCANID(mk_transfer(CanardPriorityOptional, CanardTransferKindResponse, 0b0100110011, 0b0101010),
                      0b1010101,
                      7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Anonymous service transfers not permitted.
            makeCANID(mk_transfer(CanardPriorityExceptional, CanardTransferKindRequest, 0b0100110011, 0b0101010),
                      CANARD_NODE_ID_UNSET,
                      7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Broadcast service transfers not permitted.
            makeCANID(mk_transfer(CanardPrioritySlow, CanardTransferKindResponse, 0b0100110011, CANARD_NODE_ID_UNSET),
                      0b1010101,
                      7U));
    REQUIRE(
        -CANARD_ERROR_INVALID_ARGUMENT ==  // Bad service-ID.
        makeCANID(mk_transfer(CanardPriorityNominal, CanardTransferKindResponse, 0xFFFFU, 0b0101010), 0b1010101, 7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad priority.
            makeCANID(mk_transfer(PriorityAlias{123}.prio, CanardTransferKindResponse, 0b0100110011, 0b0101010),
                      0b1010101,
                      7U));
}

TEST_CASE("makeTailByte")
{
    using internals::makeTailByte;
    REQUIRE(0b111'00000 == makeTailByte(true, true, true, 0U));
    REQUIRE(0b111'00000 == makeTailByte(true, true, true, 32U));
    REQUIRE(0b111'11111 == makeTailByte(true, true, true, 31U));
    REQUIRE(0b011'11111 == makeTailByte(false, true, true, 31U));
    REQUIRE(0b101'11110 == makeTailByte(true, false, true, 30U));
    REQUIRE(0b001'11101 == makeTailByte(false, false, true, 29U));
    REQUIRE(0b010'00001 == makeTailByte(false, true, false, 1U));
}

TEST_CASE("roundFramePayloadSizeUp")
{
    using internals::roundFramePayloadSizeUp;
    REQUIRE(0 == roundFramePayloadSizeUp(0));
    REQUIRE(1 == roundFramePayloadSizeUp(1));
    REQUIRE(2 == roundFramePayloadSizeUp(2));
    REQUIRE(3 == roundFramePayloadSizeUp(3));
    REQUIRE(4 == roundFramePayloadSizeUp(4));
    REQUIRE(5 == roundFramePayloadSizeUp(5));
    REQUIRE(6 == roundFramePayloadSizeUp(6));
    REQUIRE(7 == roundFramePayloadSizeUp(7));
    REQUIRE(8 == roundFramePayloadSizeUp(8));
    REQUIRE(12 == roundFramePayloadSizeUp(9));
    REQUIRE(12 == roundFramePayloadSizeUp(10));
    REQUIRE(12 == roundFramePayloadSizeUp(11));
    REQUIRE(12 == roundFramePayloadSizeUp(12));
    REQUIRE(16 == roundFramePayloadSizeUp(13));
    REQUIRE(16 == roundFramePayloadSizeUp(14));
    REQUIRE(16 == roundFramePayloadSizeUp(15));
    REQUIRE(16 == roundFramePayloadSizeUp(16));
    REQUIRE(20 == roundFramePayloadSizeUp(17));
    REQUIRE(20 == roundFramePayloadSizeUp(20));
    REQUIRE(32 == roundFramePayloadSizeUp(30));
    REQUIRE(32 == roundFramePayloadSizeUp(32));
    REQUIRE(48 == roundFramePayloadSizeUp(40));
    REQUIRE(48 == roundFramePayloadSizeUp(48));
    REQUIRE(64 == roundFramePayloadSizeUp(50));
    REQUIRE(64 == roundFramePayloadSizeUp(64));
}

TEST_CASE("findTxQueueSupremum")
{
    using internals::findTxQueueSupremum;
    using TxQueueItem = internals::TxQueueItem;

    auto ins = canardInit(&helpers::dummy_allocator::allocate, &helpers::dummy_allocator::free);

    const auto find = [&](std::uint32_t x) -> TxQueueItem* { return findTxQueueSupremum(&ins, x); };

    REQUIRE(nullptr == find(0));
    REQUIRE(nullptr == find((1UL << 29U) - 1U));

    TxQueueItem a{};
    a.id          = 1000;
    ins._tx_queue = reinterpret_cast<CanardInternalTxQueueItem*>(&a);

    REQUIRE(nullptr == find(999));
    REQUIRE(&a == find(1000));
    REQUIRE(&a == find(1001));

    TxQueueItem b{};
    b.id   = 1010;
    a.next = &b;

    REQUIRE(nullptr == find(999));
    REQUIRE(&a == find(1000));
    REQUIRE(&a == find(1001));
    REQUIRE(&a == find(1009));
    REQUIRE(&b == find(1010));
    REQUIRE(&b == find(1011));

    TxQueueItem c{};
    c.id          = 990;
    c.next        = &a;
    ins._tx_queue = reinterpret_cast<CanardInternalTxQueueItem*>(&c);
    REQUIRE(reinterpret_cast<TxQueueItem*>(ins._tx_queue)->id == 990);  // Make sure the list is assembled correctly.
    REQUIRE(reinterpret_cast<TxQueueItem*>(ins._tx_queue)->next->id == 1000);
    REQUIRE(reinterpret_cast<TxQueueItem*>(ins._tx_queue)->next->next->id == 1010);
    REQUIRE(reinterpret_cast<TxQueueItem*>(ins._tx_queue)->next->next->next == nullptr);

    REQUIRE(nullptr == find(989));
    REQUIRE(&c == find(990));
    REQUIRE(&c == find(999));
    REQUIRE(&a == find(1000));
    REQUIRE(&a == find(1001));
    REQUIRE(&a == find(1009));
    REQUIRE(&b == find(1010));
    REQUIRE(&b == find(1011));
}
