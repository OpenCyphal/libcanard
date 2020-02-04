// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#pragma once

#include "canard.h"
#include "catch.hpp"
#include <cstdint>

namespace internals
{
extern "C" {
std::uint32_t makeMessageSessionSpecifier(const std::uint16_t subject_id, const std::uint8_t src_node_id);
std::uint32_t makeServiceSessionSpecifier(const std::uint16_t service_id,
                                          const bool          request_not_response,
                                          const std::uint8_t  src_node_id,
                                          const std::uint8_t  dst_node_id);
}

}  // namespace internals
