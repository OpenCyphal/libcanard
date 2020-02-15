// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include "catch.hpp"
#include <cstdarg>
#include <cstdint>

/// Definitions that are not exposed by the library but that are needed for testing.
/// Please keep them in sync with the library by manually updating as necessary.
namespace internals
{
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

// Extern C effectively discards the outer namespaces.
extern "C" {

auto crcAdd(const std::uint16_t crc, const std::size_t size, const void* const bytes) -> std::uint16_t;

auto makeMessageSessionSpecifier(const std::uint16_t subject_id, const std::uint8_t src_node_id) -> std::uint32_t;
auto makeServiceSessionSpecifier(const std::uint16_t service_id,
                                 const bool          request_not_response,
                                 const std::uint8_t  src_node_id,
                                 const std::uint8_t  dst_node_id) -> std::uint32_t;

auto getPresentationLayerMTU(const CanardInstance* const ins) -> std::size_t;

auto makeCANID(const CanardTransfer* const transfer,
               const std::uint8_t          local_node_id,
               const std::size_t           presentation_layer_mtu) -> std::int32_t;

auto makeTailByte(const bool         start_of_transfer,
                  const bool         end_of_transfer,
                  const bool         toggle,
                  const std::uint8_t transfer_id) -> std::uint8_t;

auto roundFramePayloadSizeUp(const std::size_t x) -> std::size_t;

auto findTxQueueSupremum(const CanardInstance* const ins, const std::uint32_t can_id) -> TxQueueItem*;
}
}  // namespace internals
