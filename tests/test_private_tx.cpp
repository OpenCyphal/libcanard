// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 OpenCyphal Development Team.

#include "exposed.hpp"
#include "helpers.hpp"
#include "catch.hpp"

TEST_CASE("SessionSpecifier")
{
    REQUIRE(0b000'00'0'11'1001100110011'0'1010101 ==
            exposed::txMakeMessageSessionSpecifier(0b1001100110011, 0b1010101));
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==
            exposed::txMakeServiceSessionSpecifier(0b0100110011, true, 0b1010101, 0b0101010));
    REQUIRE(0b000'10'0100110011'1010101'0101010 ==
            exposed::txMakeServiceSessionSpecifier(0b0100110011, false, 0b0101010, 0b1010101));
}

TEST_CASE("adjustPresentationLayerMTU")
{
    REQUIRE(63 == exposed::adjustPresentationLayerMTU(64));
    REQUIRE(7 == exposed::adjustPresentationLayerMTU(0));
    REQUIRE(63 == exposed::adjustPresentationLayerMTU(255));
    REQUIRE(31 == exposed::adjustPresentationLayerMTU(32));
    REQUIRE(31 == exposed::adjustPresentationLayerMTU(30));
}

TEST_CASE("txMakeCANID")
{
    using exposed::txMakeCANID;

    CanardTransferMetadata meta{};

    const auto mk_meta = [&](const CanardPriority     priority,
                             const CanardTransferKind kind,
                             const std::uint16_t      port_id,
                             const std::uint8_t       remote_node_id) {
        meta.priority       = priority;
        meta.transfer_kind  = kind;
        meta.port_id        = port_id;
        meta.remote_node_id = remote_node_id;
        return &meta;
    };

    union PriorityAlias
    {
        std::uint8_t   bits;
        CanardPriority prio;
    };

    // MESSAGE TRANSFERS
    REQUIRE(0b000'00'0'11'1001100110011'0'1010101 ==  // Regular message.
            txMakeCANID(mk_meta(CanardPriorityExceptional,
                                CanardTransferKindMessage,
                                0b1001100110011,
                                CANARD_NODE_ID_UNSET),
                        0,
                        "",
                        0b1010101,
                        7U));
    REQUIRE(
        0b111'00'0'11'1001100110011'0'1010101 ==  // Regular message.
        txMakeCANID(mk_meta(CanardPriorityOptional, CanardTransferKindMessage, 0b1001100110011, CANARD_NODE_ID_UNSET),
                    0,
                    "",
                    0b1010101,
                    7U));
    REQUIRE(
        (0b010'01'0'11'1001100110011'0'0000000U | (exposed::crcAdd(0xFFFFU, 3, "\x01\x02\x03") & CANARD_NODE_ID_MAX)) ==
        txMakeCANID(mk_meta(CanardPriorityFast, CanardTransferKindMessage, 0b1001100110011, CANARD_NODE_ID_UNSET),
                    3,
                    "\x01\x02\x03",
                    128U,  // Invalid local node-ID --> anonymous message.
                    7U));
    REQUIRE(
        -CANARD_ERROR_INVALID_ARGUMENT ==  // Multi-frame anonymous messages are not allowed.
        txMakeCANID(mk_meta(CanardPriorityImmediate, CanardTransferKindMessage, 0b1001100110011, CANARD_NODE_ID_UNSET),
                    8,
                    "\x01\x02\x03\x04\x05\x06\x07\x08",
                    128U,  // Invalid local node-ID is treated as anonymous/unset.
                    7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad remote node-ID -- unicast messages not supported.
            txMakeCANID(mk_meta(CanardPriorityHigh, CanardTransferKindMessage, 0b1001100110011, 123U), 0, "", 0U, 7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad subject-ID.
            txMakeCANID(mk_meta(CanardPriorityLow, CanardTransferKindMessage, 0xFFFFU, CANARD_NODE_ID_UNSET),
                        0,
                        "",
                        0U,
                        7U));
    REQUIRE(
        -CANARD_ERROR_INVALID_ARGUMENT ==  // Bad priority.
        txMakeCANID(mk_meta(PriorityAlias{123}.prio, CanardTransferKindMessage, 0b1001100110011, CANARD_NODE_ID_UNSET),
                    0,
                    "",
                    0b1010101,
                    7U));

    // SERVICE TRANSFERS
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==  // Request.
            txMakeCANID(mk_meta(CanardPriorityExceptional, CanardTransferKindRequest, 0b0100110011, 0b0101010),
                        0,
                        "",
                        0b1010101,
                        7U));
    REQUIRE(0b111'10'0100110011'0101010'1010101 ==  // Response.
            txMakeCANID(mk_meta(CanardPriorityOptional, CanardTransferKindResponse, 0b0100110011, 0b0101010),
                        0,
                        "",
                        0b1010101,
                        7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Anonymous service transfers not permitted.
            txMakeCANID(mk_meta(CanardPriorityExceptional, CanardTransferKindRequest, 0b0100110011, 0b0101010),
                        0,
                        "",
                        CANARD_NODE_ID_UNSET,
                        7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Broadcast service transfers not permitted.
            txMakeCANID(mk_meta(CanardPrioritySlow, CanardTransferKindResponse, 0b0100110011, CANARD_NODE_ID_UNSET),
                        0,
                        "",
                        0b1010101,
                        7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad service-ID.
            txMakeCANID(mk_meta(CanardPriorityNominal, CanardTransferKindResponse, 0xFFFFU, 0b0101010),
                        0,
                        "",
                        0b1010101,
                        7U));
    REQUIRE(-CANARD_ERROR_INVALID_ARGUMENT ==  // Bad priority.
            txMakeCANID(mk_meta(PriorityAlias{123}.prio, CanardTransferKindResponse, 0b0100110011, 0b0101010),
                        0,
                        "",
                        0b1010101,
                        7U));
}

TEST_CASE("txMakeTailByte")
{
    using exposed::txMakeTailByte;
    REQUIRE(0b111'00000 == txMakeTailByte(true, true, true, 0U));
    REQUIRE(0b111'00000 == txMakeTailByte(true, true, true, 32U));
    REQUIRE(0b111'11111 == txMakeTailByte(true, true, true, 31U));
    REQUIRE(0b011'11111 == txMakeTailByte(false, true, true, 31U));
    REQUIRE(0b101'11110 == txMakeTailByte(true, false, true, 30U));
    REQUIRE(0b001'11101 == txMakeTailByte(false, false, true, 29U));
    REQUIRE(0b010'00001 == txMakeTailByte(false, true, false, 1U));
}

TEST_CASE("txRoundFramePayloadSizeUp")
{
    using exposed::txRoundFramePayloadSizeUp;
    REQUIRE(0 == txRoundFramePayloadSizeUp(0));
    REQUIRE(1 == txRoundFramePayloadSizeUp(1));
    REQUIRE(2 == txRoundFramePayloadSizeUp(2));
    REQUIRE(3 == txRoundFramePayloadSizeUp(3));
    REQUIRE(4 == txRoundFramePayloadSizeUp(4));
    REQUIRE(5 == txRoundFramePayloadSizeUp(5));
    REQUIRE(6 == txRoundFramePayloadSizeUp(6));
    REQUIRE(7 == txRoundFramePayloadSizeUp(7));
    REQUIRE(8 == txRoundFramePayloadSizeUp(8));
    REQUIRE(12 == txRoundFramePayloadSizeUp(9));
    REQUIRE(12 == txRoundFramePayloadSizeUp(10));
    REQUIRE(12 == txRoundFramePayloadSizeUp(11));
    REQUIRE(12 == txRoundFramePayloadSizeUp(12));
    REQUIRE(16 == txRoundFramePayloadSizeUp(13));
    REQUIRE(16 == txRoundFramePayloadSizeUp(14));
    REQUIRE(16 == txRoundFramePayloadSizeUp(15));
    REQUIRE(16 == txRoundFramePayloadSizeUp(16));
    REQUIRE(20 == txRoundFramePayloadSizeUp(17));
    REQUIRE(20 == txRoundFramePayloadSizeUp(20));
    REQUIRE(32 == txRoundFramePayloadSizeUp(30));
    REQUIRE(32 == txRoundFramePayloadSizeUp(32));
    REQUIRE(48 == txRoundFramePayloadSizeUp(40));
    REQUIRE(48 == txRoundFramePayloadSizeUp(48));
    REQUIRE(64 == txRoundFramePayloadSizeUp(50));
    REQUIRE(64 == txRoundFramePayloadSizeUp(64));
}
