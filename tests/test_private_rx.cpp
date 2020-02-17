// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "exposed.hpp"
#include "helpers.hpp"
#include <cstring>

TEST_CASE("rxTryParseFrame")
{
    using exposed::RxFrameModel;
    using exposed::rxTryParseFrame;

    RxFrameModel model{};

    const auto parse = [&](const CanardMicrosecond          timestamp_usec,
                           const std::uint32_t              extended_can_id,
                           const std::vector<std::uint8_t>& payload) {
        static std::vector<std::uint8_t> payload_storage;
        payload_storage = payload;
        CanardFrame frame{};
        frame.timestamp_usec  = timestamp_usec;
        frame.extended_can_id = extended_can_id;
        frame.payload_size    = std::size(payload);
        frame.payload         = payload_storage.data();
        model                 = RxFrameModel{};
        return rxTryParseFrame(&frame, &model);
    };

    // MESSAGE
    REQUIRE(parse(543210U, 0U, {0, 1, 2, 3}));
    REQUIRE(model.timestamp_usec == 543210U);
    REQUIRE(model.priority == CanardPriorityExceptional);
    REQUIRE(model.transfer_kind == CanardTransferKindMessage);
    REQUIRE(model.port_id == 0U);
    REQUIRE(model.source_node_id == 0U);
    REQUIRE(model.destination_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(model.transfer_id == 3U);
    REQUIRE(!model.start_of_transfer);
    REQUIRE(!model.end_of_transfer);
    REQUIRE(!model.toggle);
    REQUIRE(model.payload_size == 3);
    REQUIRE(model.payload[0] == 0);
    REQUIRE(model.payload[1] == 1);
    REQUIRE(model.payload[2] == 2);

    // MESSAGE
    REQUIRE(parse(123456U, 0b001'00'0'110110011001100'0'0100111U, {0b101'00000U | 23U}));
    REQUIRE(model.timestamp_usec == 123456U);
    REQUIRE(model.priority == CanardPriorityImmediate);
    REQUIRE(model.transfer_kind == CanardTransferKindMessage);
    REQUIRE(model.port_id == 0b110110011001100U);
    REQUIRE(model.source_node_id == 0b0100111U);
    REQUIRE(model.destination_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(model.transfer_id == 23U);
    REQUIRE(model.start_of_transfer);
    REQUIRE(!model.end_of_transfer);
    REQUIRE(model.toggle);
    REQUIRE(model.payload_size == 0);
    // SIMILAR BUT INVALID
    REQUIRE(!parse(123456U, 0b001'00'0'110110011001100'0'0100111U, {}));                    // NO TAIL BYTE
    REQUIRE(!parse(123456U, 0b001'00'0'110110011001100'0'0100111U, {0b100'00000U | 23U}));  // BAD TOGGLE
    REQUIRE(!parse(123456U, 0b001'00'0'110110011001100'1'0100111U, {0b101'00000U | 23U}));  // BAD RESERVED 07
    REQUIRE(!parse(123456U, 0b001'00'1'110110011001100'0'0100111U, {0b101'00000U | 23U}));  // BAD RESERVED 23
    REQUIRE(!parse(123456U, 0b001'00'1'110110011001100'1'0100111U, {0b101'00000U | 23U}));  // BAD RESERVED 07 23
    REQUIRE(!parse(123456U, 0b001'01'0'110110011001100'0'0100111U, {0b101'00000U | 23U}));  // ANON NOT SINGLE FRAME

    // ANONYMOUS MESSAGE
    REQUIRE(parse(12345U, 0b010'01'0'000110011001101'0'0100111U, {0b111'00000U | 0U}));
    REQUIRE(model.timestamp_usec == 12345U);
    REQUIRE(model.priority == CanardPriorityFast);
    REQUIRE(model.transfer_kind == CanardTransferKindMessage);
    REQUIRE(model.port_id == 0b000110011001101U);
    REQUIRE(model.source_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(model.destination_node_id == CANARD_NODE_ID_UNSET);
    REQUIRE(model.transfer_id == 0U);
    REQUIRE(model.start_of_transfer);
    REQUIRE(model.end_of_transfer);
    REQUIRE(model.toggle);
    REQUIRE(model.payload_size == 0);
    // SIMILAR BUT INVALID
    REQUIRE(!parse(12345U, 0b010'01'0'110110011001100'0'0100111U, {}));                   // NO TAIL BYTE
    REQUIRE(!parse(12345U, 0b010'01'0'110110011001100'0'0100111U, {0b110'00000U | 0U}));  // BAD TOGGLE
    REQUIRE(!parse(12345U, 0b010'01'0'110110011001100'1'0100111U, {0b111'00000U | 0U}));  // BAD RESERVED 07
    REQUIRE(!parse(12345U, 0b010'01'1'110110011001100'0'0100111U, {0b111'00000U | 0U}));  // BAD RESERVED 23
    REQUIRE(!parse(12345U, 0b010'01'1'110110011001100'1'0100111U, {0b111'00000U | 0U}));  // BAD RESERVED 07 23
    REQUIRE(!parse(12345U, 0b010'01'0'110110011001100'0'0100111U, {0b101'00000U | 0U}));  // ANON NOT SINGLE FRAME

    // REQUEST
    REQUIRE(parse(999'999U, 0b011'11'0000110011'0011010'0100111U, {0, 1, 2, 3, 0b011'00000U | 31U}));
    REQUIRE(model.timestamp_usec == 999'999U);
    REQUIRE(model.priority == CanardPriorityHigh);
    REQUIRE(model.transfer_kind == CanardTransferKindRequest);
    REQUIRE(model.port_id == 0b0000110011U);
    REQUIRE(model.source_node_id == 0b0100111U);
    REQUIRE(model.destination_node_id == 0b0011010U);
    REQUIRE(model.transfer_id == 31U);
    REQUIRE(!model.start_of_transfer);
    REQUIRE(model.end_of_transfer);
    REQUIRE(model.toggle);
    REQUIRE(model.payload_size == 4);
    REQUIRE(model.payload[0] == 0);
    REQUIRE(model.payload[1] == 1);
    REQUIRE(model.payload[2] == 2);
    REQUIRE(model.payload[3] == 3);
    // SIMILAR BUT INVALID
    REQUIRE(!parse(999'999U, 0b011'11'0000110011'0011010'0100111U, {}));                                // NO TAIL BYTE
    REQUIRE(!parse(999'999U, 0b011'11'0000110011'0011010'0100111U, {0, 1, 2, 3, 0b110'00000U | 31U}));  // BAD TOGGLE
    REQUIRE(!parse(999'999U, 0b011'11'1000110011'0011010'0100111U, {0, 1, 2, 3, 0b011'00000U | 31U}));  // BAD RESERVED

    // RESPONSE
    REQUIRE(parse(888'888U, 0b100'10'0000110011'0100111'0011010U, {255, 0b010'00000U | 1U}));
    REQUIRE(model.timestamp_usec == 888'888U);
    REQUIRE(model.priority == CanardPriorityNominal);
    REQUIRE(model.transfer_kind == CanardTransferKindResponse);
    REQUIRE(model.port_id == 0b0000110011U);
    REQUIRE(model.source_node_id == 0b0011010U);
    REQUIRE(model.destination_node_id == 0b0100111U);
    REQUIRE(model.transfer_id == 1U);
    REQUIRE(!model.start_of_transfer);
    REQUIRE(model.end_of_transfer);
    REQUIRE(!model.toggle);
    REQUIRE(model.payload_size == 1);
    REQUIRE(model.payload[0] == 255);
    // SIMILAR BUT INVALID
    REQUIRE(!parse(888'888U, 0b100'10'0000110011'0100111'0011010U, {}));                        // NO TAIL BYTE
    REQUIRE(!parse(888'888U, 0b100'10'0000110011'0100111'0011010U, {255, 0b100'00000U | 1U}));  // BAD TOGGLE
    REQUIRE(!parse(888'888U, 0b100'10'1000110011'0100111'0011010U, {255, 0b010'00000U | 1U}));  // BAD RESERVED
}

TEST_CASE("rxSessionWritePayload")
{
    using helpers::Instance;
    using exposed::RxSession;
    using exposed::rxSessionWritePayload;
    using exposed::rxSessionRestart;

    Instance  ins;
    RxSession rxs;
    rxs.toggle_and_transfer_id = 0U;

    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);

    // Regular write, the RX state is uninitialized so a new allocation will take place.
    REQUIRE(0 == rxSessionWritePayload(&ins.getInstance(), &rxs, 10, 5, "\x00\x01\x02\x03\x04"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 10);
    REQUIRE(rxs.payload_size == 5);
    REQUIRE(rxs.payload != nullptr);
    REQUIRE(rxs.payload[0] == 0);
    REQUIRE(rxs.payload[1] == 1);
    REQUIRE(rxs.payload[2] == 2);
    REQUIRE(rxs.payload[3] == 3);
    REQUIRE(rxs.payload[4] == 4);

    // Appending the pre-allocated storage.
    REQUIRE(0 == rxSessionWritePayload(&ins.getInstance(), &rxs, 10, 4, "\x05\x06\x07\x08"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 10);
    REQUIRE(rxs.payload_size == 9);
    REQUIRE(rxs.payload != nullptr);
    REQUIRE(rxs.payload[0] == 0);
    REQUIRE(rxs.payload[1] == 1);
    REQUIRE(rxs.payload[2] == 2);
    REQUIRE(rxs.payload[3] == 3);
    REQUIRE(rxs.payload[4] == 4);
    REQUIRE(rxs.payload[5] == 5);
    REQUIRE(rxs.payload[6] == 6);
    REQUIRE(rxs.payload[7] == 7);
    REQUIRE(rxs.payload[8] == 8);

    // Implicit truncation -- too much payload, excess ignored.
    REQUIRE(0 == rxSessionWritePayload(&ins.getInstance(), &rxs, 10, 3, "\x09\x0A\x0B"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 10);
    REQUIRE(rxs.payload_size == 10);
    REQUIRE(rxs.payload != nullptr);
    REQUIRE(rxs.payload[0] == 0);
    REQUIRE(rxs.payload[1] == 1);
    REQUIRE(rxs.payload[2] == 2);
    REQUIRE(rxs.payload[3] == 3);
    REQUIRE(rxs.payload[4] == 4);
    REQUIRE(rxs.payload[5] == 5);
    REQUIRE(rxs.payload[6] == 6);
    REQUIRE(rxs.payload[7] == 7);
    REQUIRE(rxs.payload[8] == 8);
    REQUIRE(rxs.payload[9] == 9);

    // Storage is already full, write ignored.
    REQUIRE(0 == rxSessionWritePayload(&ins.getInstance(), &rxs, 10, 3, "\x0C\x0D\x0E"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 1);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 10);
    REQUIRE(rxs.payload_size == 10);
    REQUIRE(rxs.payload != nullptr);
    REQUIRE(rxs.payload[0] == 0);
    REQUIRE(rxs.payload[1] == 1);
    REQUIRE(rxs.payload[2] == 2);
    REQUIRE(rxs.payload[3] == 3);
    REQUIRE(rxs.payload[4] == 4);
    REQUIRE(rxs.payload[5] == 5);
    REQUIRE(rxs.payload[6] == 6);
    REQUIRE(rxs.payload[7] == 7);
    REQUIRE(rxs.payload[8] == 8);
    REQUIRE(rxs.payload[9] == 9);

    // Restart frees the buffer. The transfer-ID will be incremented, too.
    rxSessionRestart(&ins.getInstance(), &rxs);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);
    REQUIRE(rxs.payload_size == 0);
    REQUIRE(rxs.payload == nullptr);
    REQUIRE(rxs.calculated_crc == 0xFFFFU);
    REQUIRE(rxs.toggle_and_transfer_id == 33);

    // Double restart has no effect on memory.
    rxs.calculated_crc         = 0x1234U;
    rxs.toggle_and_transfer_id = 23;
    rxSessionRestart(&ins.getInstance(), &rxs);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);
    REQUIRE(rxs.payload_size == 0);
    REQUIRE(rxs.payload == nullptr);
    REQUIRE(rxs.calculated_crc == 0xFFFFU);
    REQUIRE(rxs.toggle_and_transfer_id == (32U | 24U));

    // Restart with a transfer-ID overflow.
    rxs.calculated_crc         = 0x1234U;
    rxs.toggle_and_transfer_id = 31;
    rxSessionRestart(&ins.getInstance(), &rxs);
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);
    REQUIRE(rxs.payload_size == 0);
    REQUIRE(rxs.payload == nullptr);
    REQUIRE(rxs.calculated_crc == 0xFFFFU);
    REQUIRE(rxs.toggle_and_transfer_id == 32U);

    // Write into a zero-capacity storage. NULL at the output.
    REQUIRE(0 == rxSessionWritePayload(&ins.getInstance(), &rxs, 0, 3, "\x00\x01\x02"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);
    REQUIRE(rxs.payload_size == 0);
    REQUIRE(rxs.payload == nullptr);

    // Write with OOM.
    ins.getAllocator().setAllocationCeiling(5);
    REQUIRE(-CANARD_ERROR_OUT_OF_MEMORY == rxSessionWritePayload(&ins.getInstance(), &rxs, 10, 3, "\x00\x01\x02"));
    REQUIRE(ins.getAllocator().getNumAllocatedFragments() == 0);
    REQUIRE(ins.getAllocator().getTotalAllocatedAmount() == 0);
    REQUIRE(rxs.payload_size == 0);
    REQUIRE(rxs.payload == nullptr);
}
