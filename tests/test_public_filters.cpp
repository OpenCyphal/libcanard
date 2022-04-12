// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2021 OpenCyphal Development Team.

#include "exposed.hpp"
#include "catch.hpp"

namespace
{
constexpr std::uint32_t OFFSET_SUBJECT_ID  = 8U;
constexpr std::uint32_t OFFSET_SERVICE_ID  = 14U;
constexpr std::uint32_t OFFSET_DST_NODE_ID = 7U;

constexpr std::uint32_t FLAG_SERVICE_NOT_MESSAGE = std::uint32_t(1) << 25U;
constexpr std::uint32_t FLAG_RESERVED_23         = std::uint32_t(1) << 23U;
constexpr std::uint32_t FLAG_RESERVED_07         = std::uint32_t(1) << 7U;

TEST_CASE("FilterSubject")
{
    const std::uint16_t heartbeat_subject_id = 7509;
    CanardFilter        heartbeat_config     = canardMakeFilterForSubject(heartbeat_subject_id);
    REQUIRE((heartbeat_config.extended_can_id &
             static_cast<std::uint32_t>(heartbeat_subject_id << OFFSET_SUBJECT_ID)) != 0);
    REQUIRE((heartbeat_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((heartbeat_config.extended_mask & FLAG_RESERVED_07) != 0);
    REQUIRE((heartbeat_config.extended_mask & (CANARD_SUBJECT_ID_MAX << OFFSET_SUBJECT_ID)) != 0);
}

TEST_CASE("FilterService")
{
    const std::uint16_t access_service_id = 7509;
    const std::uint16_t node_id           = 42;
    CanardFilter        access_config     = canardMakeFilterForService(access_service_id, node_id);
    REQUIRE((access_config.extended_can_id & static_cast<std::uint32_t>(access_service_id << OFFSET_SERVICE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & static_cast<std::uint32_t>(node_id << OFFSET_DST_NODE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_RESERVED_23) != 0);
    REQUIRE((access_config.extended_mask & static_cast<std::uint32_t>(CANARD_SERVICE_ID_MAX << OFFSET_SERVICE_ID)) !=
            0);
    REQUIRE((access_config.extended_mask & static_cast<std::uint32_t>(CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID)) != 0);
}

TEST_CASE("FilterServices")
{
    const std::uint8_t node_id       = 42;
    CanardFilter       access_config = canardMakeFilterForServices(node_id);
    REQUIRE((access_config.extended_can_id & static_cast<std::uint32_t>(node_id << OFFSET_DST_NODE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_RESERVED_23) != 0);
    REQUIRE((access_config.extended_mask & static_cast<std::uint32_t>(CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID)) != 0);
}

TEST_CASE("Consolidate")
{
    const std::uint16_t heartbeat_subject_id = 7509;
    CanardFilter        heartbeat_config     = canardMakeFilterForSubject(heartbeat_subject_id);

    const std::uint16_t access_service_id = 384;
    const std::uint8_t  node_id           = 42;
    CanardFilter        access_config     = canardMakeFilterForService(access_service_id, node_id);

    CanardFilter combined = canardConsolidateFilters(&heartbeat_config, &access_config);
    REQUIRE((combined.extended_mask | heartbeat_config.extended_mask) == heartbeat_config.extended_mask);
    REQUIRE((combined.extended_mask | access_config.extended_mask) == access_config.extended_mask);
}
}  // namespace
