// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2021 UAVCAN Development Team.

#include "exposed.hpp"
#include "helpers.hpp"
#include "catch.hpp"

constexpr uint32_t OFFSET_PRIORITY    = 26U;
constexpr uint32_t OFFSET_SUBJECT_ID  = 8U;
constexpr uint32_t OFFSET_SERVICE_ID  = 14U;
constexpr uint32_t OFFSET_DST_NODE_ID = 7U;

constexpr uint32_t FLAG_SERVICE_NOT_MESSAGE  = (UINT32_C(1) << 25U);
constexpr uint32_t FLAG_ANONYMOUS_MESSAGE    = (UINT32_C(1) << 24U);
constexpr uint32_t FLAG_REQUEST_NOT_RESPONSE = (UINT32_C(1) << 24U);
constexpr uint32_t FLAG_RESERVED_23          = (UINT32_C(1) << 23U);
constexpr uint32_t FLAG_RESERVED_07          = (UINT32_C(1) << 7U);

TEST_CASE("FilterSubject")
{
    const uint16_t heartbeat_subject_id = 7509;
    CanardFilter   heartbeat_config     = canardMakeFilterForSubject(heartbeat_subject_id);
    REQUIRE((heartbeat_config.extended_can_id & (uint32_t) (heartbeat_subject_id << OFFSET_SUBJECT_ID)) != 0);
    REQUIRE((heartbeat_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((heartbeat_config.extended_mask & FLAG_RESERVED_07) != 0);
    REQUIRE((heartbeat_config.extended_mask & (CANARD_SUBJECT_ID_MAX << OFFSET_SUBJECT_ID)) != 0);
}

TEST_CASE("FilterService")
{
    const uint16_t access_service_id = 7509;
    const uint16_t node_id           = 42;
    CanardFilter   access_config     = canardMakeFilterForService(access_service_id, node_id);
    REQUIRE((access_config.extended_can_id & (uint32_t) (access_service_id << OFFSET_SERVICE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & (uint32_t) (node_id << OFFSET_DST_NODE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_RESERVED_23) != 0);
    REQUIRE((access_config.extended_mask & (uint32_t) (CANARD_SERVICE_ID_MAX << OFFSET_SERVICE_ID)) != 0);
    REQUIRE((access_config.extended_mask & (uint32_t) (CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID)) != 0);
}

TEST_CASE("FilterServices")
{
    const uint8_t node_id       = 42;
    CanardFilter  access_config = canardMakeFilterForServices(node_id);
    REQUIRE((access_config.extended_can_id & (uint32_t) (node_id << OFFSET_DST_NODE_ID)) != 0);
    REQUIRE((access_config.extended_can_id & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_SERVICE_NOT_MESSAGE) != 0);
    REQUIRE((access_config.extended_mask & FLAG_RESERVED_23) != 0);
    REQUIRE((access_config.extended_mask & (uint32_t) (CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID)) != 0);
}

TEST_CASE("Consolidate")
{
    const uint16_t heartbeat_subject_id = 7509;
    CanardFilter   heartbeat_config     = canardMakeFilterForSubject(heartbeat_subject_id);

    const uint16_t access_service_id = 7509;
    const uint8_t  node_id           = 42;
    CanardFilter   access_config     = canardMakeFilterForService(access_service_id, node_id);

    CanardFilter combined = canardConsolidateFilters(&heartbeat_config, &access_config);
    REQUIRE((combined.extended_mask | heartbeat_config.extended_mask) == heartbeat_config.extended_mask);
    REQUIRE((combined.extended_mask | access_config.extended_mask) == access_config.extended_mask);
}
