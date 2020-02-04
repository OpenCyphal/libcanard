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
// Extern C effectively discards the outer namespaces.
extern "C" {
std::uint32_t makeMessageSessionSpecifier(const std::uint16_t subject_id, const std::uint8_t src_node_id);
std::uint32_t makeServiceSessionSpecifier(const std::uint16_t service_id,
                                          const bool          request_not_response,
                                          const std::uint8_t  src_node_id,
                                          const std::uint8_t  dst_node_id);

std::uint16_t crcAdd(const std::uint16_t crc, const std::uint8_t* const bytes, const std::size_t size);
}

}  // namespace internals
