// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include <cstdarg>

namespace helpers
{
namespace dummy_allocator
{
inline auto allocate(CanardInstance* const ins, std::size_t const amount) -> void*
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

inline auto rejectAllRxFilter(const CanardInstance* ins,
                              std::uint16_t         port_id,
                              CanardTransferKind    transfer_kind,
                              std::uint8_t          source_node_id) -> CanardRxMetadata
{
    (void) ins;
    (void) port_id;
    (void) transfer_kind;
    (void) source_node_id;
    return CanardRxMetadata{};
}

}  // namespace helpers
