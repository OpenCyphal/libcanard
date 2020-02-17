// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include "catch.hpp"
#include <cstdarg>
#include <cstdint>

/// Definitions that are not exposed by the library but that are needed for testing.
/// Please keep them in sync with the library by manually updating as necessary.
namespace exposed
{
using TransferCRC = std::uint16_t;

struct TxQueueItem final
{
    TxQueueItem* next = nullptr;

    std::uint64_t deadline_usec = 0;
    std::size_t   payload_size  = 0;
    std::uint32_t id            = 0;

    std::array<std::uint8_t, 1> payload{};  // The real definition has a flex array here.

    [[nodiscard]] auto getPayloadByte(const std::size_t offset) const -> std::uint8_t
    {
        return *(payload.data() + offset);
    }

    [[nodiscard]] auto getTailByte() const
    {
        REQUIRE(payload_size >= 1U);
        return getPayloadByte(payload_size - 1U);
    }

    [[nodiscard]] auto isStartOfTransfer() const { return (getTailByte() & 128U) != 0; }
    [[nodiscard]] auto isEndOfTransfer() const { return (getTailByte() & 64U) != 0; }
    [[nodiscard]] auto isToggleBitSet() const { return (getTailByte() & 32U) != 0; }

    ~TxQueueItem()                   = default;
    TxQueueItem(const TxQueueItem&)  = delete;
    TxQueueItem(const TxQueueItem&&) = delete;
    auto operator=(const TxQueueItem&) -> TxQueueItem& = delete;
    auto operator=(const TxQueueItem &&) -> TxQueueItem& = delete;
};

struct CanardInternalRxSession
{
    CanardMicrosecond transfer_timestamp_usec   = std::numeric_limits<std::uint64_t>::max();
    std::size_t       payload_size              = 0U;
    std::uint8_t*     payload                   = nullptr;
    TransferCRC       calculated_crc            = 0U;
    CanardTransferID  toggle_and_transfer_id    = std::numeric_limits<std::uint8_t>::max();
    std::uint8_t      redundant_transport_index = std::numeric_limits<std::uint8_t>::max();
};

struct RxFrameModel
{
    CanardMicrosecond   timestamp_usec      = std::numeric_limits<std::uint64_t>::max();
    CanardPriority      priority            = CanardPriorityOptional;
    CanardTransferKind  transfer_kind       = CanardTransferKindMessage;
    CanardPortID        port_id             = std::numeric_limits<std::uint16_t>::max();
    CanardNodeID        source_node_id      = CANARD_NODE_ID_UNSET;
    CanardNodeID        destination_node_id = CANARD_NODE_ID_UNSET;
    CanardTransferID    transfer_id         = std::numeric_limits<std::uint8_t>::max();
    bool                start_of_transfer   = false;
    bool                end_of_transfer     = false;
    bool                toggle              = false;
    std::size_t         payload_size        = 0U;
    const std::uint8_t* payload             = nullptr;
};

// Extern C effectively discards the outer namespaces.
extern "C" {

auto crcAdd(const std::uint16_t crc, const std::size_t size, const void* const bytes) -> std::uint16_t;

auto txMakeMessageSessionSpecifier(const std::uint16_t subject_id, const std::uint8_t src_node_id) -> std::uint32_t;
auto txMakeServiceSessionSpecifier(const std::uint16_t service_id,
                                   const bool          request_not_response,
                                   const std::uint8_t  src_node_id,
                                   const std::uint8_t  dst_node_id) -> std::uint32_t;

auto txGetPresentationLayerMTU(const CanardInstance* const ins) -> std::size_t;

auto txMakeCANID(const CanardTransfer* const transfer,
                 const std::uint8_t          local_node_id,
                 const std::size_t           presentation_layer_mtu) -> std::int32_t;

auto txMakeTailByte(const bool         start_of_transfer,
                    const bool         end_of_transfer,
                    const bool         toggle,
                    const std::uint8_t transfer_id) -> std::uint8_t;

auto txRoundFramePayloadSizeUp(const std::size_t x) -> std::size_t;

auto txFindQueueSupremum(const CanardInstance* const ins, const std::uint32_t can_id) -> TxQueueItem*;

auto rxTryParseFrame(const CanardFrame* const frame, RxFrameModel* const out_result) -> bool;

auto rxSessionWritePayload(CanardInstance* const          ins,
                           CanardInternalRxSession* const rxs,
                           const std::size_t              payload_size_max,
                           const std::size_t              payload_size,
                           const void* const              payload) -> std::int8_t;
}
}  // namespace exposed
